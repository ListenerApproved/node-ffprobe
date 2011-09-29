#! /usr/bin/env perl

use warnings;
use strict;

use File::Spec::Functions;
use List::Util qw(sum max min);
use Getopt::Long;
use Statistics::Descriptive qw (sum mean variance max min);
# Getopt::Long::Configure ("bundling");

use Pod::Usage;

GetOptions (
    # print a short help
    'help|usage|?|h' => sub { pod2usage ( { -verbose => 1, -exitval => 0 }) },
    
    # print the manpage 
    'manpage|m' => sub { pod2usage ( { -verbose => 2, -exitval => 0 }) }
    
    # issue an error message and exit
    ) or pod2usage( { -message=> "Parsing error", 
		      -verbose => 1, -exitval => 1 });


my $ffprobe_cmd="ffprobe -show_frames";

# my $input_file;
# if (@ARGV) {
#     $input_file= shift @ARGV;
# } else {
#     $input_file = "";
# }

sub print_frames_sizes {
    my ($outfile, $frames_ref) = @_;

    open (DAT, ">$outfile") or die "Can't open $outfile";
    foreach my $frame (@$frames_ref) {
        print DAT "$frame->{stream_pkt_nb} ",  $frame->{size} * 8 /1000, "\n";
    }
    close(DAT);
}

open(STATS, "$ffprobe_cmd @ARGV|") or die "Couldn't exec the ffprobe command on @ARGV: $!"; 

# algorithm: skip all the lines up to the "[FRAME] one
# put all the following pairs (key value) in the frame hash
# when encounter the next [FRAME]
# is implemented as a finite state machine
sub parse_frames {
    my @frames;
    my $frame = {};             # empty frame

    # skip all lines different from "[FRAME]"
  limbo:
    while(<STATS>) {
        goto inside_frame if /^\[FRAME\]/;
    }
    goto end;

  inside_frame:
    while (<STATS>) {
        if (/^\[\/FRAME\]/) {       # end of frame
            push @frames, $frame;
            $frame= {};             # create a new frame
            goto limbo;
        } else {
            my ($key, $val) = /[ ]*([^=]*)=(.*)/;
#              print "$key->$val\n";       
            $$frame{$key}=$val;
        }
    }
    goto end;
  end:
    close(STATS);
    return @frames;
}


my @frames = parse_frames();

my %vstats;
my $tmpdir = File::Spec->tmpdir();
foreach my $frame (@frames) {
    next unless ($$frame{codec_type} eq "video");

    my $type= $$frame{pict_type};
#     print "$type ";
    if (not $vstats{$type}) {
        # create a new hash reference
        $vstats{$type} = { };
        $vstats{$type}->{dat_filename} = File::Spec->catfile($tmpdir, "$type-sizes.dat");
        my @ary = ();
        $vstats{$type}->{vframes} = \@ary;
    }
    push @{$vstats{$type}->{vframes}}, $frame;    
}

foreach my $type (keys %vstats) {
    print_frames_sizes ($vstats{$type}->{dat_filename},
                        $vstats{$type}->{vframes});
}

# compute the average size of each type of frame
foreach my $type (keys %vstats) {
    my $vstats_handler= Statistics::Descriptive::Full->new();
    my $ary_ref = $vstats{$type}->{vframes};
    $vstats_handler->add_data ( map $_->{size}, @$ary_ref);
    $vstats{$type}->{mean_size} = $vstats_handler->mean();
    print "$type frames mean size:  $vstats{$type}->{mean_size}\n";
    print("$type frames number:  ", scalar @{$vstats{$type}->{vframes}}, "\n");
}

# create a gnuplot script
my $gnuplot_script= File::Spec->catfile($tmpdir, "script.gnuplot");
   
# write the gnuplot script
open (GNUPLOT_SCRIPT, ">$gnuplot_script") or die "Couldn't open $gnuplot_script: $!";
print GNUPLOT_SCRIPT << "EOF";
set title "video frame sizes"
set xlabel "frame number"
set ylabel "frame size (Kbits)"
EOF

# assemblate the string for the plot command
my @strings;
foreach my $type (keys %vstats) {
    my $file=$vstats{$type}->{dat_filename};
    my $title="$type frames";
    push @strings, "\"$file\" title \"$title\"";
}

$"= ", ";
print GNUPLOT_SCRIPT "plot @strings";
close(GNUPLOT_SCRIPT);

# load the script, then launch an interactive gnuplot session
close (STDIN);
open (STDIN, "<", "/dev/tty") or die "Can't open /dev/tty: $!";
system ("gnuplot", "$gnuplot_script",  "-");

# just without an interactive session
# system ("gnuplot", "$gnuplot_script");

foreach my $type (keys %vstats) {
    my $file=$vstats{$type}->{dat_filename};
    unlink $file or print STDERR "Couldn't delete $file: $!\n";
}

=head1 NAME
    
plot-video-frames-stats.pl - Uses ffprobe and gnuplot to print a representation of the video frames in a multimedia file
    
=head1 SYNOPSIS
    
plot-video-frames.pl <options> <input-file>
        
=head1 OPTIONS
    
=over 4

=item B<--help|--usage|-h|-?>

Print a brief help message and exits.
    
=back
    
=head1 DESCRIPTION
    
B<This program> plots a representation of the video frames size relatives to a
ffprobe stats file in input.

Use it as follows:
    ffprobe -frames file.in > file.stats
    plot-video-frames-stats.pl file.stats

or simply:
    ffprobe -frames file.in | plot-video-frames-stats.pl

=cut
