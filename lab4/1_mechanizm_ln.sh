mkdir some_project && cd some_project;
mkdir versions && cd versions;
mkdir v1 v2 v3;
touch v1/README.md v2/README.md v3/README.md;
cd ..;

ln -s versions/v1 current_version;

mkdir hardlinks && ln $(readlink current_version)/* hardlinks/;

ln -sf versions/v2 current_version;
# что произошло: мы перезаписали софтлинку (симлинку (символьную ссылку))