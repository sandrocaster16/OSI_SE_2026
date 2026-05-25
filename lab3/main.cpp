#include <iostream>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <ctime>
#include <iomanip>
#include <sys/stat.h>
#include <fcntl.h>

volatile sig_atomic_t stop_flag = false;
volatile sig_atomic_t hup_flag = false;
volatile sig_atomic_t usr1_flag = false;
volatile sig_atomic_t usr2_flag = false;

// логер
class Logger{
private:
    static void log_message(std::string what, std::string s){
        std::time_t now = std::time(nullptr);
        log_file<<"["<<std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S")<<"] "<<what<<":\t"<<s<<'\n';

        log_file.flush();
    }

public:
    static std::ofstream log_file;

    static bool init(const std::string& path){
        log_file.open(path, std::ios::app);
        return log_file.is_open();
    }

    static void print_log(std::string s){
        log_message("LOG", s);
    }

    static void print_info(std::string s){
        log_message("INFO", s);
    }

    static void print_error(std::string s){
        log_message("ERROR", s);
    }

    // ротация логов
    static void rotate_log(){
        if(log_file.is_open())
            log_file.close();

        std::filesystem::rename("/tmp/pws.log", "/tmp/pws.log.old");

        log_file.open("/tmp/pws.log", std::ios::app);
        print_info("Произошла ротация лога");
    }
};
std::ofstream Logger::log_file;


// config
struct {
    std::filesystem::path PID_file = "/tmp/pws.pid";
    std::filesystem::path fifo_path = "/tmp/pws_fifo";
    std::filesystem::path target_file;
    bool run_as_daemon = false;
} config;


// правильный выход
void exit_program(){
    Logger::print_info("Программа завершена");
    std::filesystem::remove(config.PID_file);
    std::filesystem::remove(config.fifo_path);
}

// обработчик сигналов
void signal_handler(int signal){
    if(signal == SIGTERM || signal == SIGINT){
        stop_flag = true;
    }
    else if(signal == SIGHUP){
        hup_flag = true;
    }
    else if(signal == SIGUSR1){
        usr1_flag = true;
    }
    else if(signal == SIGUSR2){
        usr2_flag = true;
    }
}

void reopen_target_file(std::ifstream& source, std::streampos& last_pos){
    Logger::print_info("Переоткрываем таргет файл");

    source.close();
    source.open(config.target_file);
    source.clear();

    source.seekg(0, std::ios::end);
    last_pos = source.tellg();

    Logger::print_info("Файл переоткрыт");
}

// демонизация
void daemonize(){
    pid_t pid = fork();

    if(pid < 0)
        exit(EXIT_FAILURE);
    if(pid > 0)
        exit(EXIT_SUCCESS);

    if(setsid() < 0)
        exit(EXIT_FAILURE);

    // права доступа к созданным файлам от демона
    umask(0);

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}

int main(int argc, char** argv){
    // обработки сигналов
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT, signal_handler);
    std::signal(SIGHUP, signal_handler);
    std::signal(SIGUSR1, signal_handler);
    std::signal(SIGUSR2, signal_handler);

    if(!Logger::init("/tmp/pws.log")){
        std::cerr<<"Не удалось открыть файл лога /tmp/pws.log\n";

        return 1;
    }

    // start
    std::cout<<"Process Watcher Service\n";

    // махинация с PID
    if(std::filesystem::exists(config.PID_file)){
        Logger::print_error("Программа уже запущена");

        return 0;
    }

    // првоерка файла
    if(argc < 2 || argc > 3){
        std::cerr<<"Программа запущена неправильно: а./pws <путь к файлу> [-d]\n";
        Logger::print_error("Программа запущена неправильно: ./pws <путь к файлу> [-d]");

        exit_program();
        return 1;
    }

    config.target_file = argv[1];

    if(!std::filesystem::exists(config.target_file)){
        std::cerr<<"Файла "+std::string(config.target_file)+" не существует\n";
        Logger::print_error("Файла "+std::string(config.target_file)+" не существует");

        exit_program();
        return 1;
    }

    // -d and демонайзер
    if(argc == 3 && std::string(argv[2]) == "-d"){
        config.run_as_daemon = true;
    }

    if(config.run_as_daemon)
        daemonize();

    // записываем пид
    std::ofstream file(config.PID_file);
    file<<getpid();
    file.close();

    Logger::print_info("Программа запущена. PID: "+std::to_string(getpid()));

    // FIFO
    mkfifo(config.fifo_path.c_str(), 0666);
    int fifo_fd = open(config.fifo_path.c_str(), O_RDONLY | O_NONBLOCK);

    // начало отслежки
    std::ifstream source(config.target_file);
    source.seekg(0, std::ios::end); // конец файла чек

    std::streampos last_pos = source.tellg();
    int lines = 0;

    while(!stop_flag){
        // обработка сигналов рантайм
        if(usr1_flag){
            Logger::print_info("PID программы: "+std::to_string(getpid()));
            Logger::print_info("Обработано "+std::to_string(lines)+" строк");
            usr1_flag = false;
        }
        if(usr2_flag){
            Logger::rotate_log();
            usr2_flag = false;
        }
        if(hup_flag){
            reopen_target_file(source, last_pos);
            hup_flag = false;
        }

        // FIFO
        char buffer[256];
        int bytes_read = read(fifo_fd, buffer, sizeof(buffer) - 1);
        if(bytes_read > 0){
            buffer[bytes_read] = '\0';
            std::string cmd(buffer);
            cmd.erase(cmd.find_last_not_of(" \n\r\t") + 1);

            Logger::print_info("получена команда в FIFO "+cmd);

            if(cmd == "STOP")
                stop_flag = true;
            else if(cmd == "STATUS")
                usr1_flag = true;
            else if(cmd == "CHANGE_MODE")
                hup_flag = true;
            else
                Logger::print_error("Некорректный или неизвестный ввод в FIFO");
        }

        // отслеживаемый файл
        source.clear();
        source.seekg(0, std::ios::end);

        if(source.tellg() > last_pos){
            source.seekg(last_pos);

            std::string line;
            while(std::getline(source, line)){
                if(!line.empty()){
                    Logger::print_log("Новая строка ["+std::to_string(lines+1)+"]: "+line);
                    ++lines;
                }
            }

            source.clear();
            last_pos = source.tellg();
        }

        sleep(1);
    }

    // выход
    exit_program();

    return 0;
}