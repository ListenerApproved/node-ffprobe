#!/bin/sh

svn_revision=`./show-version.sh`

NEW_REVISION="#define FFPROBE_VERSION \"SVN-r$svn_revision\""
OLD_REVISION=`cat src/version.h 2> /dev/null`

# Update version.h only on revision changes to avoid spurious rebuilds
if test "$NEW_REVISION" != "$OLD_REVISION"; then
    echo "$NEW_REVISION" > src/version.h
fi
