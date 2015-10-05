#!/bin/bash
# Written by Ares Drake, ares.drake@gmail.com
# Tweaked by Brian Eilber, brian.eilber@gmail.com
# Licenced under GPL v3
# This Script shall be called as MythTV user job. It transcodes
# the DVB recordings (mpeg files) using Handbrake. It first checks
# whether the recording is HDTV. If so it will be reencoded with
# H.264 to save space. SDTV will have commercials cut out if
# necessary and will then be transcoded to H.264 (Xvid, DivX). 

#USAGE:######################
# This Sript shall be called as a MythTV user job like as follows:
# /path/to/mythbrake.sh "%DIR%" "%FILE%" "%CHANID%" "%STARTTIMEUTC%" "%TITLE%" "%SUBTITLE%" "%CATEGORY%"
#############################


#REQUIREMENTS################
# You need to have the following programs installed:
# mediainfo: http://mediainfo.sourceforge.net/
# handbrake with dependencies: http://www.handbrake.fr
# Installation of these is convered on their sites
#############################


######SOME CONSTANSTS FOR USER EDITING######
logdir="/var/tmp" #change to your needs for logs
errormail="spanky@spankythehero.com" # this email address will be informed in case of errors
maildebug=0
outdir="/storage/videos/recordings" # specify directory where you want the transcoded file to be placed
######END constants for user editing######

#mysql> select * from recorded where basename like '1058_20140608043000.mpg' \G
#*************************** 1. row ***************************
#         chanid: 1058
#      starttime: 2014-06-08 04:30:00
#        endtime: 2014-06-08 05:00:00
#          title: Samurai Jack
#       subtitle: XIX
#    description: Jack comes upon the ruins of his former village.
#         season: 0
#        episode: 0
#       category: Children
#       hostname: Helium
#       bookmark: 0
#        editing: 0
#        cutlist: 0
#     autoexpire: 1
#    commflagged: 1
#       recgroup: Default
#       recordid: 33
#       seriesid: EP00451702
#      programid: EP004517020024
#        inetref: 75164
#   lastmodified: 2014-06-08 05:00:10
#       filesize: 485545156
#          stars: 0
#previouslyshown: 1
#originalairdate: 2002-09-13
#       preserve: 0
#         findid: 0
#  deletepending: 0
#     transcoder: 0
#    timestretch: 1
#    recpriority: 0
#       basename: 1058_20140608043000.mpg
#      progstart: 2014-06-08 04:30:00
#        progend: 2014-06-08 05:00:00
#      playgroup: Default
#        profile: Default
#      duplicate: 1
#     transcoded: 0
#        watched: 0
#   storagegroup: Default
# bookmarkupdate: 0000-00-00 00:00:00
#1 row in set (0.00 sec)

#mysql --batch -e "update recorded set basename = '$filename' where chanid = $chanid and starttime = $starttime"


# Parse /etc/mythtv/mysql.txt - wip
DBHost=localhost
DBPort=3306
DBUserName=mythtv
DBName=mythconverg
DBPassword=mythtv

#DEFINE SOME BASIC VARIABLES
scriptstarttime=$(date +%F-%H%M%S)
mythrecordingsdir="$1" # specify directory where MythTV stores its recordings
file="$2"
infile="$mythrecordingsdir/$file"

if [ ${infile: -4} == ".mp4" ] 
then
exit 288
fi

# using sed to sanitize the variables to avoid problematic file names, only alphanumerical, space, hyphen and underscore allowed, other characters are transformed to underscore
subtitle="$(echo "$6" | sed 's/[^A-Za-z0-9_ -]/_/g')"
title="$(echo "$5" | sed 's/[^A-Za-z0-9_ -]/_/g')"
category="$(echo "$7" | sed 's/[^A-Za-z0-9_ -]/_/g')" 
chanid="$3"
starttime="$4"
if [ -z "$category" ]
then
category="Unknown" #name for unknown category, Unbekannt is German for unknown
fi
logfile="$logdir/$scriptstarttime-$title.log"
touch "$logfile"
chown mythtv:mythtv "$logfile"
chmod a+rw "$logfile"
#ex filename 1606_20140527140000.mpg
filename="$(echo "$file" | sed 's/mpg/mp4/g')" # can be customized
if [ -f "$mythrecordingsdir/$filename" ]
 # do not overwrite outfile, if already exists, change name
 then
 filename="$(echo "$filename" |sed 's/.mp4/$scriptstarttime.mp4/g')"
fi
outfile="$mythrecordingsdir/$filename"
#Variables finished

#DO SOME LOGGING
echo "Transcode job $title starting at $scriptstarttime" |tee -a "$logfile"
echo "Original file: $mythrecordingsdir/$file" |tee -a "$logfile"
echo "Target file: $outfile" |tee -a "$logfile"
echo "ChanId: $chanid Time: $starttime" |tee -a "$logfile"
echo "Filename: $filename"  |tee -a "$logfile"
echo "Infile: $mythrecordingsdir/$file Outfile: $outfile" |tee -a "$logfile"

######SOURCEFILE CHECK######
if [ ! -f "$mythrecordingsdir/$file" ]
then
    #source file does not exist
    scriptstoptime=$(date +%F-%H%M%S)
    echo "Error at $scriptstoptime: Source file not found " |tee -a "$logfile"
    echo "Maybe wrong path or missing permissions?" |tee -a "$logfile"
    mail -s "Mythtv Sourcefile Error on $HOSTNAME" "$errormail" < "$logfile"
    mv "$logfile" "$logfile-FAILED"
    exit 1
fi

echo "Userjob Mythbrake Encoding starts" 2|tee -a "$logfile"

HandBrakeCLI -e x264 -f mp4 -i "$mythrecordingsdir/$file" -o "$outfile" --preset "Normal" 2>> "$logfile"
if [ $? != 0 ]
then
	if [ ! -f "$outfile" ]
	then
		scriptstoptime=$(date +%F-%H%M%S)
		echo "Transcoding-Error at $scriptstoptime" |tee -a "$logfile"
		echo "Interrupted file $outfile" |tee -a "$logfile"
		echo "###################################" |tee -a "$logfile"
		mail -s "Mythtv No Output File Error on $HOSTNAME" "$errormail" < "$logfile"
	        mv "$logfile" "$logfile-FAILED"
		exit 1
	else
		scriptstoptime=$(date +%F-%H%M%S)
        	echo "Transcoding-Error at $scriptstoptime" |tee -a "$logfile"
	        echo "Interrupted file $outfile" |tee -a "$logfile"
	        echo "###################################" |tee -a "$logfile"
	        mail -s "Mythtv No Output File Error on $HOSTNAME" "$errormail" < "$logfile"
	        mv "$logfile" "$logfile-FAILED"
	        exit 1
	fi
else
	echo "Transcode Run successfull." |tee -a "$logfile"
	file "$outfile" |tee -a "$logfile"
	scriptstoptime=$(date +%F-%H%M%S)
	echo "Successfully finished at $scriptstoptime" |tee -a "$logfile"
	echo "Transcoded file: $outfile" |tee -a "$logfile"
	#Transcoding now done, following is some maintenance work
	echo "Updating database records..." |tee -a "$logfile"
	filesize=$(stat -c%s "$outfile")
	mysql -h $DBHost -u $DBUserName -p$DBPassword $DBName -e "update recorded set basename = '$filename' where basename like '$file'" 2>&1 | tee -a "$logfile"
	mysql -h $DBHost -u $DBUserName -p$DBPassword $DBName -e "update recorded set transcoded = 1 where basename like '$filename'" 2>&1 | tee -a "$logfile"
	mysql -h $DBHost -u $DBUserName -p$DBPassword $DBName -e "update recorded set filesize = '$filesize' where basename like '$filename'" 2>&1 | tee -a "$logfile"
	chown mythtv:mythtv $outfile  2>&1 | tee -a "$logfile"
	chmod 664 $outfile  2>&1 | tee -a "$logfile"
	mythcommflag --file $outfile  2>&1 | tee -a "$logfile"
	mythcommflag --file $outfile --rebuild  2>&1 | tee -a "$logfile"
	rm -f $mythrecordingsdir/$file*
fi
	
exit 0
