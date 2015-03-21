#!/bin/bash
# by Brian Eilber, brian.eilber@gmail.com
# Licenced under GPL v3

#USAGE:######################
# Run in recording directory of master backend
#############################

#REQUIREMENTS################
# Master Backend
#############################


OIFS=$IFS

for file in `ls | grep -v png`
do
TITLE=""
SUBTITLE=""
SEASON=""
EPISODE=""
SUBTITLEORIG=""
INETREF=""
OUTNAME=""
	TITLE=$(mysql -u mythtv -pmythtv mythconverg -se "select title from recorded where basename like '${file}'" | tr -d '[:punct:]')
	SUBTITLE=$(mysql -u mythtv -pmythtv mythconverg -se "select subtitle from recorded where basename like '${file}'"| tr -d '[:punct:]')
	SUBTITLEORIG=$(mysql -u mythtv -pmythtv mythconverg -se "select subtitle from recorded where basename like '${file}'")
	SEASON=$(mysql -u mythtv -pmythtv mythconverg -se "select lpad (season, 2,'0') from recorded where basename like '${file}'"| tr -d '[:punct:]')
	EPISODE=$(mysql -u mythtv -pmythtv mythconverg -se "select lpad (episode, 2,'0') from recorded where basename like '${file}'"| tr -d '[:punct:]')
	INETREF=$(mysql -u mythtv -pmythtv mythconverg -se "select inetref from recorded where basename like '${file}'")

	if  [[ "${TITLE}" == "" &&  "${SUBTITLE}" == "" ]] 
	then
		if [[ "${SEASON}" == "" &&  "${EPISODE}" == "" ]]
		then
			echo "DB Lookup Failed"
			#rm -vf $file
		fi
	else
		if [ "${SEASON}" == "00" ] || [ "${EPISODE}" == "00" ]
		then
			if [[ "${SUBTITLEORIG}"  =~ ';' ]]
			then
				STARTEP="0"
				ENDEP="0"
				while IFS=';' read -ra ITEM
				do
					for ELEMENT in "${ITEM[@]}"
					do
						SEASON=$(/usr/share/mythtv/metadata/Television/ttvdb.py -l en -a US -N "${INETREF}" "${ELEMENT}" | xmllint --xpath 'string(//metadata/item/season)' -)
						SEASON=$(printf %02d ${SEASON})
						EPISODE=$(/usr/share/mythtv/metadata/Television/ttvdb.py -l en -a US -N "${INETREF}" "${ELEMENT}" | xmllint --xpath 'string(//metadata/item/episode)' -)
						EPISODE=$(printf %02d ${EPISODE})
						if [ "${STARTEP}" -eq  0 ]
						then
							STARTEP="${EPISODE}"
						fi
						
						if [ "${EPISODE}" -ge "${STARTEP}" ]
						then
							if [ "${EPISODE}" -gt "${ENDEP}" ]
							then
								ENDEP="${EPISODE}"
							fi
						else
							STARTEP="${EPISODE}"
						fi
							
					done
					if [ "${SEASON}" != "00" ]
					then
						if [ "${STARTEP}" != "00" ] || [ "${ENDEP}" != "00" ]
						then
							if [ "${STARTEP}" != "${ENDEP}" ]
							then
								filename="${TITLE} - s${SEASON}e${STARTEP}-e${ENDEP} - ${SUBTITLE}.${file: -3}"
								path="/storage/series/${TITLE}/SEASON $SEASON"
								mkdir -p "${path}"
								testname=`echo ${filename} | sed 's/....$//'`
								if [ "${testname}" != "- se - " ]
								then
									mv -v $file "${path}/${filename}"
								fi
							fi
						fi
					fi
				done <<< "${SUBTITLEORIG}"
				
			fi
			
		else
			path="/storage/series/${TITLE}/SEASON $SEASON"
			filename="${TITLE} - s${SEASON}e${EPISODE} - ${SUBTITLE}.${file: -3}"
			mkdir -p "${path}"
			testname=`echo ${filename} | sed 's/....$//'`
			if [ "${testname}" != "- se - " ]
			then
				echo "${path}/${filename}"
#				mv $file "${path}/${filename}"
				#mysql -u mythtv -pmythtv mythconverg -e "delete from recorded where basename like '${file}'"
			fi
		fi
	fi
done 
IFS=$OIFS
