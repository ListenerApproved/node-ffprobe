#! /usr/bin/env perl

use warnings;
use strict;

use File::Spec::Functions;
use List::Util qw(sum max min);
use Getopt::Long;
# use Statistics::Descriptive qw (sum mean variance max min);
# Getopt::Long::Configure ("bundling");

use Pod::Usage;

my $frames_num = 10;

GetOptions (
    # print a short help
    'help|usage|?|h' => sub { pod2usage ( { -verbose => 1, -exitval => 0 }) },
    
    # print the manpage 
    'manpage|m' => sub { pod2usage ( { -verbose => 2, -exitval => 0 }) }, 
    'frames_num|n=i' => \$frames_num, 

    # issue an error message and exit
    ) or pod2usage( { -message=> "Parsing error", 
		      -verbose => 1, -exitval => 1 });

# put here the files to remove at the end of the script
my @files_to_remove;

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

# algorithm: skip all the lines up to the "[FRAME] one
# put all the following pairs (key value) in the frame hash
# when encounter the next [FRAME]
# is implemented as a finite state machine

# parse_frames ($filter_ref)
# $filter_ref is a reference to a function which is applied to the
# frame before inserting it; the frame ref is inserted in the returned
# @frames list only if filter_ref evaluation is 1.
sub parse_frames {
    my ($file, $filter_ref) = @_;

    my $ffprobe_cmd="ffprobe -show_frames";
    open(STATS, "$ffprobe_cmd $file|") or die "Couldn't exec the ffprobe command on $file: $!"; 

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
            push(@frames, $frame) if ($filter_ref->($frame) eq 1);
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

# parse_streams ($filter_ref)
# $filter_ref is a reference to a function which is applied to each
# streams stanza before inserting it in the streams array; the stream
# ref is inserted in the returned @streams list only if filter_ref
# evaluation returns 1.
sub parse_streams {
    my ($file, $filter_ref) = @_;

    my $ffprobe_cmd="ffprobe -show_streams";
    open(STATS, "$ffprobe_cmd $file|") or die "Couldn't exec the ffprobe command on $file: $!"; 

    my @streams;
    my $stream = {};             # empty stream

    # skip all lines different from "[STREAM]"
  limbo:
    while(<STATS>) {
        goto inside_stream if /^\[STREAM\]/;
    }
    goto end;

  inside_stream:
    while (<STATS>) {
        if (/^\[\/STREAM\]/) {       # end of stream
            push(@streams, $stream) if ($filter_ref->($stream) eq 1);
            $stream= {};             # create a new hash reference
            goto limbo;
        } else {
            my ($key, $val) = /[ ]*([^=]*)=(.*)/;
#             print "$key->$val\n";       
            $$stream{$key}=$val;
        }
    }
    goto end;
  end:
    close(STATS);
    return @streams;
}

# consider only the first file in input
my $file = shift @ARGV;

my @vstreams = parse_streams(
    $file,
    sub {
        my $stream = shift;
        return 1 if $stream->{codec_type} eq "video";
        return 0;
    }
    );

my $vstream;
if (scalar @vstreams > 1) {
    die "More than one video stream in $file!!\n";
} elsif (scalar @vstreams < 1) {
    die "No video stream in $file!!\n";
} else {                        # exactly one video stream
    $vstream = shift @vstreams; 
}

my @vframes = parse_frames(
    $file,
    sub {
        my $frame = shift;
        return 1 if ($frame->{codec_type} eq "video");
        return 0; 
    }
    );

# this buffer is used to store the last $frames_num video frames.
my @buffer;
my $buffer_size = 0;    

my $tmpdir = File::Spec->tmpdir();
my $dat_filename = File::Spec->catfile($tmpdir, "bitrates.dat");
push @files_to_remove, $dat_filename;

open (DAT, ">$dat_filename") or die "Can't open $dat_filename";
foreach my $vframe (@vframes) {
    push @buffer, $vframe;
    $buffer_size += $vframe->{size};
 
    # remove the last frame from the buffer if too big
    if ($frames_num > 0 and scalar @buffer > $frames_num) {
        my $old_vframe = shift @buffer;
        $buffer_size -= $old_vframe->{size};
    }

    my $average_vframe_size = $buffer_size / (scalar @buffer); # bytes
    my $average_vframes_bitrate = $average_vframe_size * $vstream->{r_frame_rate_num} / $vstream->{r_frame_rate_den}; # bytes
    print DAT $average_vframes_bitrate * 8 / 1000, "\n"; # Kbits
}
close(DAT);

# create a gnuplot script
my $gnuplot_script= File::Spec->catfile($tmpdir, "script.gnuplot");
push @files_to_remove, $gnuplot_script;
   
# write the gnuplot script
$frames_num="infinite" if $frames_num < 0;
open (GNUPLOT_SCRIPT, ">$gnuplot_script") or die "Couldn't open $gnuplot_script: $!";
print GNUPLOT_SCRIPT << "EOF";
set title "$file"
set xlabel "frame number"
set ylabel "average video frame bitrate (Kbits) (last $frames_num video frames)"
plot \"$dat_filename\" title \"Average video frames bitrate ($vstream->{r_frame_rate})\"
EOF

close(GNUPLOT_SCRIPT);

# load the script, then launch an interactive gnuplot session
close (STDIN);
open (STDIN, "<", "/dev/tty") or die "Can't open /dev/tty: $!";
system ("gnuplot", "$gnuplot_script",  "-");

foreach my $file (@files_to_remove) {
    unlink $file or print STDERR "Couldn't delete $file: $!\n";
}

=head1 NAME
    
plot-vframes-bitrate - Plot video frames bitrate of a video stream
    
=head1 SYNOPSIS
    
plot-vframes-bitrate <options> <input-file>
        
=head1 OPTIONS
    
=over 4

=item B<--help|--usage|-h|-?>

Print a brief help message and exit.

=item B<-m|--manpage>

Print the program manpage and exit.

=item B<--frames_num|-n> 

Set the number of video frames to consider when computing the average
bitrate. Default value is 10.  A negative value corresponds to
infinite, that is will be considered all the video frames preceding
the current one (and the bitrates will tend to the global average value). 

=back
    
=head1 DESCRIPTION
    
B<This program> uses ffprobe and gnuplot in combination to plot the
average bitrate of a video stream computed in the last frames_num
video frames (the value specified with the --frames-num parameter).

=head1 SEE ALSO

L<ffmpeg(1)>, L<ffprobe(1)>, L<mpeg4ip(1)>, L<gnuplot(1)>

=cut
