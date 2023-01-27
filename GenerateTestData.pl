use strict;
use warnings;

use Cwd qw/getcwd abs_path/;
use Win32::Process;
use File::Path;

my $PERL = $^X;
my $SCRIPT = abs_path($0);
$SCRIPT =~ s/\//\\/g;

my $READ_ONLY = 0;

my $TOTAL_FILES = 1024 * 1024;
my $DIR_COUNT = $ENV{NUMBER_OF_PROCESSORS} * 8;

if(scalar(@ARGV) == 1)
{
    # Child process
    my $dstFolder = shift(@ARGV);

    my $fileCount = $TOTAL_FILES / $DIR_COUNT;

    for(my $i = 0; $i < $fileCount; ++$i)
    {
        my $fileName = sprintf("%s/%04X.txt", $dstFolder, $i);

        my $fd;
        open($fd, "> $fileName") or die($?);
        close($fd);

        if($READ_ONLY)
        {
            chmod(0444, $fileName);
        }
    }

    exit(0);
}

my @children;

sub HandleCancel()
{
    foreach my $child (@children)
    {
        $child->Kill(0)
    }

    exit(1);
}

$SIG{INT} = \&HandleCancel;

my $cwd = getcwd();

for(my $i = 0; $i < $DIR_COUNT; ++$i)
{
    my $dir = sprintf("%s/TestData/%04X", $cwd, $i);

    unless(-d $dir)
    {
        mkpath($dir);
    }

    my $args = "$SCRIPT $dir";
    my $child;
    Win32::Process::Create($child, $PERL, "$PERL $args", 0, NORMAL_PRIORITY_CLASS, ".") || die;
    push(@children, $child);
}

while(@children)
{
    my $child = $children[0];
    $child->Wait(INFINITE);
    shift(@children);
}


