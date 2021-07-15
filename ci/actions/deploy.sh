#!/bin/bash

set -o errexit
set -o nounset
set -o xtrace
OS=$(uname)

if [[ "${BETA-0}" -eq 1 ]]; then
    BUILD="beta"
elif [[ "${TEST-0}" -eq 1 ]]; then
    BUILD="test"
else
    BUILD="live"
fi

if [[ "$OS" == 'Linux' ]]; then
    sha256sum $GITHUB_WORKSPACE/build/vban-node-*-Linux.tar.bz2 >$GITHUB_WORKSPACE/vban-node-$TAG-Linux.tar.bz2.sha256
    sha256sum $GITHUB_WORKSPACE/build/vban-node-*-Linux.deb >$GITHUB_WORKSPACE/vban-node-$TAG-Linux.deb.sha256
    aws s3 cp $GITHUB_WORKSPACE/build/vban-node-*-Linux.tar.bz2 s3://repo.vban.org/$BUILD/binaries/vban-node-$TAG-Linux.tar.bz2 --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
    aws s3 cp $GITHUB_WORKSPACE/vban-node-$TAG-Linux.tar.bz2.sha256 s3://repo.vban.org/$BUILD/binaries/vban-node-$TAG-Linux.tar.bz2.sha256 --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
    aws s3 cp $GITHUB_WORKSPACE/build/vban-node-*-Linux.deb s3://repo.vban.org/$BUILD/binaries/vban-node-$TAG-Linux.deb --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
    aws s3 cp $GITHUB_WORKSPACE/vban-node-$TAG-Linux.deb.sha256 s3://repo.vban.org/$BUILD/binaries/vban-node-$TAG-Linux.deb.sha256 --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
else
    sha256sum $GITHUB_WORKSPACE/build/vban-node-*-Darwin.dmg >$GITHUB_WORKSPACE/build/vban-node-$TAG-Darwin.dmg.sha256
    aws s3 cp $GITHUB_WORKSPACE/build/vban-node-*-Darwin.dmg s3://repo.vban.org/$BUILD/binaries/vban-node-$TAG-Darwin.dmg --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
    aws s3 cp $GITHUB_WORKSPACE/build/vban-node-$TAG-Darwin.dmg.sha256 s3://repo.vban.org/$BUILD/binaries/vban-node-$TAG-Darwin.dmg.sha256 --grants read=uri=http://acs.amazonaws.com/groups/global/AllUsers
fi
