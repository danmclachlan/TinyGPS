# TinyGPS

TinyGPS - a small GPS library for Arduino providing basic NMEA parsing
Based on work by and "distance_to" and "course_to" courtesy of Maarten Lamers.
Suggestion to add satellites(), course_to(), and cardinal(), by Matt Monson.
Copyright (C) 2008-2012 Mikal Hart
All rights reserved.

10/20/2023  Modifed by Dan McLachlan (drmclach@live.com)
            Applied Kevin Walton's PUBX updates to v13 and improved support for 
            PUBX,00 and PUBX,04 messages.
            Also pulled in changes from the Teensy distribution adding
            GPGSA, GNRMC, GNGNS, GNGSA, GPGSV and GLGSV
            with tracked satelites and constellations 

5/9/2012	Modified by Kevin Walton (kevin@unseen.org)
			Applied Terry Baume's PUBX updates to v12
			Updates are marked //Kevin
			Terrys sats() is replaced by v12's native satellites()
			ToDo - Add Vertical velocity parsing
			Warning, only testing so far is using the example test harness "static_test.pde"
			
9/8/2010	Modified by Terry Baume (terry@bogaurd.net)
			Support for Ublox NMEA extension PUBX 00
			Method to retrieve number of sats tracked
			Adjusted invalid lock defaults