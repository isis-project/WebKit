#
# Regular cron jobs for the webkit package
#
0 4	* * *	root	[ -x /usr/bin/webkit_maintenance ] && /usr/bin/webkit_maintenance
