#!/bin/bash

# YYYY-MM-DD_HH-MM-SS
gen_datatime(){
    date +"%Y-%m-%d_%H-%M-%S"
}

# проверка
init_check(){
if [[ ! -d $HIDE_DIR/$FILE_NAME ]]
then
    echo "система контрроля версий не инициализирована"
    exit 1
fi
}

# init hide dir
HIDE_DIR="$HOME/.fileversion"
if [[ ! -d $HIDE_DIR ]]
then
    mkdir $HIDE_DIR
fi

# perems
COMMAND=$1
FILE=$2
PARAM=$3


# main
if [[ -n $COMMAND && -n $FILE ]]
then
    FILE_NAME=$(echo $FILE | awk -F/ '{print $NF;}')
    DATETIME=$(gen_datatime)

    if [[ -f $FILE ]]
    then
        FILE_INODE=$(stat -c %i $FILE)
        FILE_SIZE=$(stat -c %s $FILE)
        FILE_HASH=$(sha256sum $FILE | awk '{print $1}')
    fi

    # init
    if [[ "$COMMAND" == "init" && -f $FILE ]]
    then
        if [[ -d $HIDE_DIR/$FILE_NAME ]]
        then
            rm -r $HIDE_DIR/$FILE_NAME
        fi

        mkdir $HIDE_DIR/$FILE_NAME
        mkdir $HIDE_DIR/$FILE_NAME/versions
        HARDLINK_NAME=v1_$DATETIME
        HARDLINK=$HIDE_DIR/$FILE_NAME/versions/$HARDLINK_NAME
        ln $FILE $HARDLINK
        ln -s $HARDLINK $HIDE_DIR/$FILE_NAME/current_version

        jq -n \
        --arg file "$FILE" \
        --arg file_name "$FILE_NAME" \
        --arg hardlink_name "$HARDLINK_NAME" \
        --arg datetime "$DATETIME" \
        --arg file_inode "$FILE_INODE" \
        --arg file_size "$FILE_SIZE" \
        --arg file_hash "$FILE_HASH" \
        '{
            filename: $file,
            basename: $file_name,
            versions:[
                {
                    id: 1,
                    name: $hardlink_name,
                    timestamp: $datetime,
                    inode: ($file_inode | tonumber),
                    size: ($file_size | tonumber),
                    comment: "Initial version",
                    hash: $file_hash
                }
            ],
            current_version: 1,
            created: $datetime,
            last_updated: $datetime
        }' \
        > "$HIDE_DIR/$FILE_NAME/metadata.json"

        echo "Version control initialized for $FILE"

    # commit
    elif [[ "$COMMAND" == "commit" && -f $FILE ]]
    then
        init_check

        METADATA="$HIDE_DIR/$FILE_NAME/metadata.json"
        CURRENT_VERSION_ID=$(jq '.current_version' "$METADATA")
        STORED_INODE=$(jq -r --arg vid "$CURRENT_VERSION_ID" '.versions[] | select(.id == ($vid | tonumber)) | .inode' "$METADATA")

        if [[ "$FILE_INODE" == "$STORED_INODE" ]]
        then
            echo "No changes"
            exit 0
        fi

        EXISTING_VERSION=$(jq -r --arg inode "$FILE_INODE" '.versions[] | select(.inode == ($inode | tonumber)) | .id' "$METADATA")

        # если есть версия с такой inode
        if [[ -n "$EXISTING_VERSION" ]]
        then
            jq --arg vid "$EXISTING_VERSION" \
               --arg datetime "$DATETIME" \
               '.current_version = ($vid | tonumber) | .last_updated = $datetime' \
               "$METADATA" > "${METADATA}.tmp" && mv "${METADATA}.tmp" "$METADATA"

            EXISTING_NAME=$(jq -r --arg vid "$EXISTING_VERSION" '.versions[] | select(.id == ($vid | tonumber)) | .name' "$METADATA")
            ln -sf "$HIDE_DIR/$FILE_NAME/versions/$EXISTING_NAME" "$HIDE_DIR/$FILE_NAME/current_version"

            echo "Committed version $EXISTING_VERSION: $PARAM"
            exit 0
        fi

        NEW_VERSION_ID=$((CURRENT_VERSION_ID+1))
        HARDLINK_NAME="v${NEW_VERSION_ID}_$DATETIME"
        HARDLINK="$HIDE_DIR/$FILE_NAME/versions/$HARDLINK_NAME"
        ln "$FILE" "$HARDLINK"

        ln -sf "$HARDLINK" "$HIDE_DIR/$FILE_NAME/current_version"

        FILE_HASH=$(sha256sum "$FILE" | awk '{print $1}')
        FILE_SIZE=$(stat -c %s "$FILE")
        COMMENT="$PARAM"

        jq --arg new_id "$NEW_VERSION_ID" \
           --arg name "$HARDLINK_NAME" \
           --arg datetime "$DATETIME" \
           --arg inode "$FILE_INODE" \
           --arg size "$FILE_SIZE" \
           --arg hash "$FILE_HASH" \
           --arg comment "$COMMENT" \
           '.current_version = ($new_id | tonumber) |
            .last_updated = $datetime |
            .versions += [
                {
                    id: ($new_id | tonumber),
                    name: $name,
                    timestamp: $datetime,
                    inode: ($inode | tonumber),
                    size: ($size | tonumber),
                    comment: $comment,
                    hash: $hash
                }
            ]' \
           "$METADATA" > "${METADATA}.tmp" && mv "${METADATA}.tmp" "$METADATA"

        echo "Committed version $NEW_VERSION_ID: $PARAM"

    # restore
    elif [[ "$COMMAND" == "restore" && -n $PARAM ]]
    then
        init_check

        METADATA="$HIDE_DIR/$FILE_NAME/metadata.json"

        if [[ "$PARAM" == "latest" ]]
        then
            VERSION=$(jq '.current_version' "$METADATA")
        else
            VERSION=$PARAM
        fi

        VERSION_NAME=$(jq -r --arg vid "$VERSION" '.versions[] | select(.id == ($vid | tonumber)) | .name' "$METADATA")
        if [[ -z "$VERSION_NAME" ]]
        then
            echo "Version $VERSION not found"
            exit 1
        fi

        VERSION_PATH="$HIDE_DIR/$FILE_NAME/versions/$VERSION_NAME"

        ln -f "$VERSION_PATH" "$FILE"

        DATETIME=$(gen_datatime)
        jq --arg vid "$VERSION" \
           --arg datetime "$DATETIME" \
           '.current_version = ($vid | tonumber) | .last_updated = $datetime' \
           "$METADATA" > "${METADATA}.tmp" && mv "${METADATA}.tmp" "$METADATA"

        ln -sf "$VERSION_PATH" "$HIDE_DIR/$FILE_NAME/current_version"

        echo "Checked out version $VERSION to $FILE"

    else
        echo "Неизвестная команда"
        exit 1
    fi
else
    exit 1
fi