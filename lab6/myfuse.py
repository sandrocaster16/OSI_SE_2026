import os
import sys
import errno
import stat
import io
import subprocess
from help import HELP
from fuse import FUSE, FuseOSError, Operations
from PIL import Image


def list_mounts():
    result = subprocess.run(['ps', 'aux'], capture_output=True, text=True)
    mounts = []

    for line in result.stdout.splitlines():
        if 'myfuse.py' in line and '--list' not in line and '-l' not in line:
            parts = line.split()

            pid = parts[1]
            orig = parts[-2]
            mount = parts[-1]

            mounts.append((pid, orig, mount))

    return mounts


class ConvertFS(Operations):
    def __init__(self, original_dir):
        self.original_dir = os.path.realpath(original_dir)
        self.cache = {}

    def _real_path(self, path):
        return os.path.join(self.original_dir, path.lstrip('/'))

    def _is_virtual_jpg(self, path):
        if not path.endswith('.jpg'):
            return False

        png_path = self._real_path(path[:-4] + '.png')
        return os.path.isfile(png_path)

    def _get_jpg_data(self, png_path):
        if png_path not in self.cache:
            img = Image.open(png_path).convert('RGB')
            buf = io.BytesIO()
            img.save(buf, format='JPEG', quality=90)
            self.cache[png_path] = buf.getvalue()

        return self.cache[png_path]



    def getattr(self, path, fh=None):
        if self._is_virtual_jpg(path):
            png_path = self._real_path(path[:-4] + '.png')
            st = os.lstat(png_path)
            jpg_data = self._get_jpg_data(png_path)

            return {
                'st_mode':  stat.S_IFREG | 0o444,
                'st_size':  len(jpg_data),
                'st_atime': st.st_atime,
                'st_mtime': st.st_mtime,
                'st_ctime': st.st_ctime,
                'st_nlink': 1,
                'st_uid':   st.st_uid,
                'st_gid':   st.st_gid,
            }

        real = self._real_path(path)
        if not os.path.exists(real):
            raise FuseOSError(errno.ENOENT)

        st = os.lstat(real)

        return dict((key, getattr(st, key)) for key in (
            'st_atime', 'st_ctime', 'st_gid', 'st_mode',
            'st_mtime', 'st_nlink', 'st_size', 'st_uid'))


    def statfs(self, path):
        real = self._real_path(path)
        stv = os.statvfs(real)

        return dict((key, getattr(stv, key)) for key in (
            'f_bavail', 'f_bfree', 'f_blocks', 'f_bsize', 'f_favail',
            'f_ffree', 'f_files', 'f_flag', 'f_frsize', 'f_namemax'))

    def readdir(self, path, fh):
        real = self._real_path(path)
        entries = ['.', '..']

        for name in os.listdir(real):
            entries.append(name)
            if name.endswith('.png') and os.path.isfile(os.path.join(real, name)):
                entries.append(name[:-4] + '.jpg')

        return entries

    def open(self, path, flags):
        if self._is_virtual_jpg(path):
            return 0

        real = self._real_path(path)

        if not os.path.exists(real):
            raise FuseOSError(errno.ENOENT)

        return 0

    def read(self, path, size, offset, fh):
        if self._is_virtual_jpg(path):
            png_path = self._real_path(path[:-4] + '.png')
            data = self._get_jpg_data(png_path)

            return data[offset:offset + size]

        real = self._real_path(path)

        with open(real, 'rb') as f:
            f.seek(offset)

            return f.read(size)

    def release(self, path, fh):
        return 0

    def create(self, path, mode):
        real = self._real_path(path)
        fd = os.open(real, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, mode)
        os.close(fd)

        return 0

    def write(self, path, data, offset, fh):
        real = self._real_path(path)
        mode = 'r+b' if os.path.exists(real) else 'w+b'

        with open(real, mode) as f:
            f.seek(offset)
            f.write(data)

        return len(data)

    def truncate(self, path, length, fh=None):
        real = self._real_path(path)

        if os.path.exists(real):
            with open(real, 'r+b') as f:
                f.truncate(length)
        else:
            with open(real, 'wb') as f:
                f.truncate(length)

    def mkdir(self, path, mode):
        return os.mkdir(self._real_path(path), mode)

    def rmdir(self, path):
        return os.rmdir(self._real_path(path))

    def unlink(self, path):
        if self._is_virtual_jpg(path):
            return os.unlink(self._real_path(path[:-4] + '.png'))
        return os.unlink(self._real_path(path))

    def rename(self, old, new):
        return os.rename(self._real_path(old), self._real_path(new))

    def chmod(self, path, mode):
        return os.chmod(self._real_path(path), mode)

    def chown(self, path, uid, gid):
        raise FuseOSError(errno.ENOTSUP)

    def link(self, target, name):
        raise FuseOSError(errno.ENOTSUP)

    def symlink(self, name, target):
        raise FuseOSError(errno.ENOTSUP)



if __name__ == '__main__':
    if len(sys.argv) == 2 and sys.argv[1] in ('-h', '--help'):
        print(HELP)
        sys.exit(0)

    if len(sys.argv) == 2 and sys.argv[1] in ('-l', '--list'):
        mounts = list_mounts()
        if not mounts:
            print('нет примонтированных директорий')
        else:
            print('примонтированные директории:')
            for pid, orig, mount in mounts:
                print(f'(PID: {pid}) {orig} -> {mount}')
        sys.exit(0)

    if len(sys.argv) == 3 and sys.argv[1] == '-u':
        mount_point = sys.argv[2]
        ret = os.system(f'fusermount -u "{mount_point}"')
        if ret == 0:
            print(f'{mount_point} отмонтирована')
        sys.exit(ret)

    if len(sys.argv) != 3:
        print("хелп: 'myfuse --help' или 'myfuse -h'")
        sys.exit(1)

    original_dir = sys.argv[1]
    mount_point = sys.argv[2]

    if not os.path.isdir(original_dir):
        print(f'{original_dir} не директория')
        sys.exit(1)

    if not os.path.isdir(mount_point):
        print(f'{mount_point} не директория')
        sys.exit(1)

    print(f'{mount_point} примонтирован к {original_dir}')
    FUSE(ConvertFS(original_dir), mount_point, nothreads=True, foreground=False)