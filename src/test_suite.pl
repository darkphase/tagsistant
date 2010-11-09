#!/usr/bin/perl

#
# test script for tagsistant
#
# using perl threads, the script starts a tagsistant instance
# on a thread, fetching all the output and feeding it to a
# file. on the main thread the script performs a sequence of
# tests and reports any error on STDERR
#

use strict;
use warnings;

use threads;
use threads::shared;

use Errno;
use POSIX;

my $FUSE_GROUP = "fuse";

# mount command
my $BIN = "./tagsistant";
my $MPOINT = "$ENV{HOME}/tags";
my $MP = $MPOINT;
my $REPOSITORY = "$ENV{HOME}/.tagsistant_test";
my $MCMD = "$BIN -d -v --repository=$REPOSITORY $MPOINT 2>&1";

# umount command
my $FUSERMOUNT = `which fusermount` || die("No fusermount found!\n");
chomp $FUSERMOUNT;
my $UMCMD = "$FUSERMOUNT -u $MPOINT";

# other global vars
my $TID = undef;
my $tc = 0;
my $tc_ok = 0;
my $tc_error = 0;
my $output = undef;
my $error_stack = "";

start_tagsistant();

print "Doing tests...\n";

# ---------[tagsistant is mounted, do tests]---------------------------- <---

my $testbed_ok = !test("ls -a $MP");

unless ($testbed_ok) {
	print "Testbed not ok!\n";
	goto EXITSUITE;
}

$testbed_ok = !test("ls -a $MP/tags");

unless ($testbed_ok) {
	print "Testbed not ok!\n";
	goto EXITSUITE;
}

out_test('^\.$', '^\.\.$');
test("ls -a $MP/archive");
out_test('^\.$', '^\.\.$');
test("ls -a $MP/stats");
out_test('^\.$', '^\.\.$');
test("ls -a $MP/relations");
out_test('^\.$', '^\.\.$');

test("mkdir $MP/tags/t1");
test("touch $MP/tags/t1");
test("ls -a $MP/tags/t1/=");
out_test('^\.$', '^\.\.$');
test("ls -a $MP/tags");
out_test('t1$');
test("cp tagsistant $MP/tags/t1/=");
test("ls -a $MP/tags/t1/=");
test("stat $MP/tags/t1/=/tagsistant");
test("stat $MP/tags/t1/=/1.tagsistant");
test("truncate -s 1 $MP/tags/t1/=/1.tagsistant");
test("chown $>:$> $MP/tags/t1/=/1.tagsistant");
test("chmod 000 $MP/tags/t1/=/1.tagsistant");
test("chmod 777 $MP/tags/t1/=/1.tagsistant");
test("chmod 755 $MP/archive/1.tagsistant");
test("mv $MP/archive/1.tagsistant $MP/archive/1.tagsistant-renamed");
test("mv $MP/archive/1.tagsistant-renamed $MP/archive/1.tagsistant");
# test("mv $MP/t1/=/1.tagsistant $MP/t1/=/1.tagsistant-renamed");
# test("mv $MP/t1/=/1.tagsistant-renamed $MP/t1/=/1.tagsistant");
test("mkdir $MP/tags/toberenamed");
test("mv $MP/tags/toberenamed $MP/tags/renamed");
test("stat $MP/tags/renamed");
test("ls $MP/tags/renamed");
test("cp /etc/motd $MP/tags/t1/=/junk");
test("rm $MP/tags/t1/=/junk");
test("cp /etc/motd $MP/tags/t1/=/junk");
test("rm $MP/archive/*junk");
test("mkdir $MP/tags/t2");
test("cp tagsistant $MP/tags/t2/=");
test("ls -a $MP/tags/t2/=");
out_test('tagsistant');
test("stat $MP/tags/t2/=/tagsistant");
test("stat $MP/tags/t2/=/1.tagsistant");
test("touch $MP/tags/t2/=/1.tagsistant");
test("cp /etc/motd $MP/tags/t1/=");
test("ls -a $MP/tags/t1/t2/=");
out_test('tagsistant', -1);
test("ls -a $MP/tags/t1/=");
out_test('motd');
$output =~ m/(\d+\.tagsistant)/m;
my $tagsistant = $1;
$output =~ m/(\d+\.motd)/m;
my $motd = $1;
test("ls -a $MP/tags/t1/=/$tagsistant");
test("ls -a $MP/tags/t1/=/$motd");
test("ls -a $MP/tags/t2/=/$tagsistant");
test("ls -a $MP/tags/t1/t2/=/$tagsistant");
test("ls -a $MP/tags/t2/t1/=/$tagsistant");
test("ls -a $MP/tags/t2/t1/+/t1/=/$tagsistant");
test("ls -a $MP/tags/t2/t1/+/t2/=/$tagsistant");

test("cat /etc/motd > $MP/tags/t1/=/buffer1");
test("diff /etc/motd $MP/tags/t1/=/buffer1");
test("ln -s $MP/tags/t1/=/buffer1 $MP/tags/t2/=/");
test("echo ciao");
test("ln -s $MP/tags/t1/=/buffer1 $MP/tags/t2/=/buffer2");
test("diff $MP/tags/t1/=/buffer1 $MP/tags/t2/=/buffer1");

system("dmesg > /tmp/clutter");
test("ln -s /tmp/clutter $MP/tags/t1/=");
test("stat $MP/tags/t1/=/clutter");
test("ls $MP/tags/t1/=/*clutter");
test("diff /tmp/clutter $MP/tags/t1/=/clutter");
test("diff /tmp/clutter $MP/tags/t1/=/*clutter");
test("diff /tmp/clutter $MP/archive/*clutter");
test("cp /tmp/clutter $MP/tags/t2/=");
test("stat $MP/tags/t2/=");

# ---------[no more test to run]---------------------------------------- <---

print "\n" x 80;
print "*" x 80;
print "\nTests done! $tc test run - $tc_ok test succeeded - $tc_error test failed, summary follows:\n";

print $error_stack;

print "\n press [ENTER] to umount tagsistant...";
<STDIN>;

EXITSUITE: stop_tagsistant();
$TID->join();

exit();

# ---------[script end, subroutines follow]-----------------------------

#
# the core of the thread running tagistant, invoked
# by start_tagsistant()
#
sub run_tagsistant {
	print "Mounting tagsistant: $MCMD...\n";
	open(TS, "$MCMD|") or die("Can't start tagsistant ($MCMD)\n");
	open(LOG, ">/tmp/tagsistant.log") or die("Can't open log file /tmp/tagsistant.log\n");

	while (<TS>) {
		print LOG $_ if /^TS/;
	}

	close(LOG);
	threads->exit();
}

#
# prepare the testbad
#
sub start_tagsistant {
	print "Creating the testbed...\n";

	#
	# check if user is part of fuse group
	#
	my $id = qx|id|;
	unless ($id =~ /\($FUSE_GROUP\)/) {
		die("User is not in $FUSE_GROUP group and is not allowed to mount a tagsistant filesystem");
	}

	#
	# remove old copy of the repository
	# each run must start from zero!
	#
	system("rm -rf $REPOSITORY 1>/dev/null 2>/dev/null");
	die("Can't remove $REPOSITORY\n") unless $? == 0;

	#
	# start tagsistant in a separate thread
	#
	$TID = threads->create(\&run_tagsistant);

	#
	# wait until tagsistant is brought to life
	#
	sleep(1);

	#
	# check if thread was properly started
	#
	unless (defined $TID and $TID) {
		die("Can't create tagsistant thread!\n");
	}
	print "Testbed running!\n";
}

sub stop_tagsistant {
	print "\nUnmounting tagsistant: $UMCMD...\n";
	system($UMCMD);
}

#
# Execute a command and check its exit code
#
sub test {
	$tc++;

	#
	# build the command
	#
	#my $command = join(" ", @_);
	my $command = shift();

	#
	# expected exit status
	#
	my $expected_exit_status = shift() || 0;

	#
	# run the command and trap the output in a global variable
	#
	$output = qx|$command 2>&1|;

	#
	# guess the operation status (OK or ERROR!)
	#
	my $status = ($? == $expected_exit_status) ? "[  OK  ]" : "[ERROR!]";

	#
	# print summary
	#
	my $status_line = "\n____ $status [#$tc] $command " . "_" x (60 - length($tc) - length($command)) . "cmd__\n\n$output";
	print $status_line;
	$error_stack .= $status_line if $status =~ /ERROR/;

	#
	# return 0 if everything went OK
	#
	if ($? == 0) {
		$tc_ok++;
		return 0;
	}

	#
	# report errors
	#
	my $signal = $? & 127;
	my $coredump = $? & 128;
	my $exitstatus = $? >> 8;
	my $strerror = strerror($exitstatus);

	if ($? == -1) {
		print " *** failed to execute: $!\n";
	} elsif ($signal) {
		printf " *** child died with signal %d, %s coredump\n", ($signal),  ($coredump) ? 'with' : 'without';
	} else {
		printf " *** child exited with value %d: %s\n", $exitstatus, $strerror;
	}

	$tc_error++;
	return $exitstatus;
}

#
# apply a list of regular expressions on the output
# of last performed command
#
sub out_test {
	my $stc = 0;
	my $status = undef;
	my $got_an_error = 0;
	$tc++;
	for my $rx (@_) {
		$stc++;
		unless ($output =~ m/$rx/m) {
			$got_an_error++;
			$status = "[ERROR!]";
		} else {
			$status = "[  OK  ]";
		}
		my $status_line = "\n \\__ $status [#$tc.$stc] /$rx/ " . "_" x (56 - length($tc) - length($stc) - length($rx)) . "__re__\n";
		print $status_line;
		$error_stack .= $status_line if $status =~ /ERROR/;
	}

	if ($got_an_error) {
		$tc_error++;
	} else {
		$tc_ok++;
	}
	return 0;
}
