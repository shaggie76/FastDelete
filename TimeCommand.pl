use strict;
use warnings;

# TimeCommand.pl cmd.exe /c rd /s /q TestData
# 41 seconds

my $elapsed = -time();
system(join(' ', @ARGV));
$elapsed += time();

print("Elapsed: $elapsed seconds\n");
