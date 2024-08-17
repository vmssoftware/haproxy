$ set noverify
$
$ delete/log/noconf *.pcsi;*
$ delete/log/noconf *.pcsi$compressed;*
$
$ product package -
/format=sequential -
/source=haproxy-x86.pcsi$desc -
/destination=[] -
/opt=noconfirm -
/material=([],[-]) -
haproxy
$
$ product copy haproxy/dest=[]/format=compressed/opt=noconfirm
$ purge/log
$ exit
