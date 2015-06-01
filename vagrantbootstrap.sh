#!/bin/bash

apt-get update
apt-get install -y libssl-dev pkg-config libfuse2 libfuse-dev \
	sqlite3 sqlite3-doc libsqlite3-dev
ln -sf /vagrant /home/vagrant/dedupfs
