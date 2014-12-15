#!/usr/bin/perl -w
# mythtv.firewire.channels.pl v1.0
# By: John Baab
# Email: john.baab@gmail.com
# Purpose: 
#
# License:
#
# This Package is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This package is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public
# License along with this package; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
#
# On Debian & Ubuntu systems, a complete copy of the GPL can be found under
# /usr/share/common-licenses/GPL-2, or (at your option) any later version

use DBI;
use DBD::mysql;
use MythTV;
use strict;

# Set default values
my $debug;
##################################
#                                #
#    Main code starts here !!    #
#                                #
##################################

my $usage = "\nHow to use mythtv.firewire.channels.pl : \n"
    ."\nExample: mythtv.firewire.channels.pl\n";

# get this script's ARGS
#

my $num = $#ARGV + 1;

# if user hasn't passed enough arguments, die and print the usage info
#if ($num <= 2) {
#    die "$usage";
#}

#
# Get all the arguments
#

foreach (@ARGV){

    if ($_ =~ m/debug/) {
        $debug = 1;
    }
}

#
#
my $myth = new MythTV();
# connect to database
my $connect = $myth->{'dbh'};

my $query = "SELECT count(distinct(ch.channum))
FROM channel ch
join cardinput ci on ci.sourceid = ch.sourceid
join capturecard cc on cc.cardid = ci.cardid
where cc.cardtype = 'Firewire'";
#print $query;
my $query_handle = $connect->prepare($query);

# EXECUTE THE QUERY
$query_handle->execute() || die "Cannot connect to database \n";

my $totalChans = $query_handle->fetchrow_array;
print "$totalChans ";

# PREPARE THE QUERY
$query = "SELECT distinct(ch.channum), ch.callsign
FROM channel ch
join cardinput ci on ci.sourceid = ch.sourceid
join capturecard cc on cc.cardid = ci.cardid
where cc.cardtype = 'Firewire'
order by ch.channum+0";
#print $query;
$query_handle = $connect->prepare($query);

# EXECUTE THE QUERY
$query_handle->execute() || die "Cannot connect to database \n";

while ( my ($channum, $callsign) = $query_handle->fetchrow_array ) {
	print "$channum $callsign ";
}
