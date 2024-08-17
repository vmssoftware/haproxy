$ set verify
$
$ purge/log
$ delete/log/noconf *.exe;*
$ delete/log/noconf *.obj;*
$ delete/log/noconf *.lis;*
$ delete/log/noconf *.olb;*
$ delete/log/noconf *.map;*
$ delete/log/noconf *.log;*
$
$ delete/log/noconf [.cxx_repository]cxx$demangler_db.;*
$ delete/log/noconf cxx_repository.dir;*
$
$ delete/log/noconf [.vms]*.pcsi;
$ delete/log/noconf [.vms]*.pcsi$compressed;
$
$ exit
