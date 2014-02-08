#!/usr/bin/env ruby

WEBDAV="https://webdav.yandex.ru/"

MP1="mp1"
MP2="mp2"

FILE1="#{MP1}/file"
FILE2="#{MP2}/file"

def shell cmd
	puts ">> #{cmd}"
	`#{cmd}`
	return $?
end

def init
	finish
	shell "mkdir -p #{MP1} && ./wdfs -o cachedir=#{MP1}_cache #{WEBDAV} #{MP1}"
	shell "mkdir -p #{MP2} && ./wdfs -o cachedir=#{MP2}_cache #{WEBDAV} #{MP2}"
end

def finish
	`fusermount -u #{MP1}`
	`fusermount -u #{MP2}`
	`sudo umount #{MP1}`
	`sudo umount #{MP2}`
end

def copy_test
	shell "rm #{FILE1}"
	shell "rm #{FILE2}"

	shell "cp file #{FILE1}"

	shell "ls -l #{MP1}"
	shell "ls -l #{MP2}"

	shell "cp #{FILE2} file2"

	result = shell "diff file file2"
	if result != 0
		raise "files differ"
	end

	
ensure
	shell "rm #{FILE1}"
	shell "rm #{FILE2}"
end

begin
	init

	`dd bs=1024 count=512 if=/dev/urandom of=file`
	`cp file file1_first`

	copy_test

	`dd bs=1024 count=512 if=/dev/urandom of=file`

	copy_test


ensure
	sleep 3
	finish
end


