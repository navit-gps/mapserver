;; mapserverd for navit project
;; this daemon intended to work with map catalogs and
;; maps itself
;;
;; (c) 2016 Alexander Vdolainen <avdolainen@zoho.com>
;;

;; Configurate the daemon
(create-var-group 'daemon)
;; set the logfile
(var-set (daemon/logfile "daemon.log"))

;; use log file right now
(zd-open-logfile)

;; Global variables group
(create-var-group 'global)
;; Test variable to define name of the module
(var-set (global/sxmpdsoname "/usr/local/lib/libmodsxmp.so.0"))
(var-set (global/datamuxsoname "/usr/local/lib/libmoddatamux.so.0"))
(var-set (global/pgsqlbesoname "/usr/local/lib/libmodpsqlbe.so.0"))

;; Modules
(module-add 'network sxmp)
(module-add 'datamux sxmp)
(module-add 'psqlbe sxmp)

;; Configure modules
;; modules symbol prefixes and path
(module-set 'master (:prefix "sxmpd")(:pathname global/sxmpdsoname))
(module-set 'datamux (:prefix "datamux")(:pathname global/datamuxsoname))
(module-set 'psqlbe (:prefix "psqlbe")(:pathname global/pgsqlbesoname))

;; time to load them
(module-load 'network)
(module-load 'datamux)
(module-load 'psqlbe)

;; SXMP network and x.509 configuration below
(sxmpd-instance-add 'main)
(sxmpd-set-port 'main 1313) ;; set port for main instance
(sxmpd-set-rootca 'main "navitpub.pem")
(sxmpd-set-daemonca 'main "maps.crt")

;; TODO: add auth functions

;; ----
;; Data storage settings
;; ----

;; Add postgres data backend
(psqlbe-db-add "mapserverdb") ;; database used by postgresql backend
(psqlbe-db-set-host "mapserverdb" "localhost")
(psqlbe-db-set-name "mapserverdb" "mapdata")
(psqlbe-db-set-user "mapserverdb" "daemon")
(psqlbe-db-set-password "mapserverdb" "foobarsecret")

;; ok, now let's add data we store
;; some data will be used for auth (since it's cool to admin it)
;; other data for maps catalog, logs and so on ...
;;
;; Auth data
;; X.509 certificates store:
;; access-rights - access rights entry
;; pemid - ID of X.509
;; attr - is a bitmask (public - used just to get the maps, admin - used to administer server others reserved)
;; uid - user belong to X.509 if any
;; filename - filename of X.509 itself
(dm-define-object 'x509 (:acc 'access-rights)(:pemid 'u64)(:attr 'u8)(:uid 'u32)
                  (:filename 'cstr 256))
;; backend host this data, here we pin x509 object to store the data in mapserverdb
(dm-set-object-store-backend 'x509 (:be (object 'psqlbe psqlbe-ops))(:key "mapserverdb"))
;; set cache size for the data, this is useful to cache all the data
(dm-set-object-cache 'x509 128kb)

;; user object
(dm-define-object 'user (:acc 'access-rights)(:uid 'u32)(:gid 'u32)(:sal 'u8)(:attr 'u8)
                  (:domainid 'u8)(:reserved 'u8)(:rid 'u32)
                  (:login 'cstr 32)(:password 'cstr 46)(:salt 'cstr 32)
                  (:expdate 'u64)(:pemid 'u64)(:spec 'u64)(:gids 'u32 8))
(dm-set-object-store-backend 'user (:be (object 'psqlbe psqlbe-ops))(:key "mapserverdb"))
(dm-set-object-cache 'user 2048kb)

;; group object
(dm-define-object 'group (:acc 'access-rights)(:gid 'u32)(:rid 'u32)(:sal 'u8)(:attr 'u8)
                  (:domainid 'u8)(:reserved 'u8)(:name 'cstr 32)(:spec 'u64))
(dm-set-object-store-backend 'group (:be (object 'psqlbe psqlbe-ops))(:key "mapserverdb"))
(dm-set-object-cache 'group 512kb)

;; role, here we are able to store data about available RPC channels to avoid recompiling
;; code
(dm-define-object 'role (:acc 'access-rights)(:rid 'u32)(:attr 'u8)(:domainid 'u8)
                  (:channels 'u16 512)(:name 'cstr 64))
(dm-set-object-store-backend 'role (:be (object 'psqlbe psqlbe-ops))(:key "mapserverdb"))
(dm-set-object-cache 'role 2048kb)

;; ok, misc stuff is done
;; now about maps
;; Map catalog
;; i guess to have a possibility to have a different regions catalogs, custom maps
;; and so on - better to have ability to manage catalogs
(dm-define-object 'mapdirectory (:acc 'access-rights)(:mdid 'u32)(:name 'cstr 512))
(dm-set-object-store-backend 'mapdirectory (:be (object 'psqlbe psqlbe-ops))(:key "mapserverdb"))
(dm-set-object-cache 'mapdirectory 128kb)

;; Each entry i.e. region of the map is a sector
;; here comments required to do it right ...
;; Map entries
(dm-define-object 'mapregion (:acc 'access-rights)(:mdid 'u32)(:name 'cstr 512)
                  (:mrid 'u64)(:rootmrid 'u64)(:navitmap 'cstr 128)(:generation 'u32)
                  (:mpsize 'u64)(:source 'u64))
(dm-set-object-store-backend 'mapregion (:be (object 'psqlbe psqlbe-ops))(:key "mapserverdb"))
(dm-set-object-cache 'mapregion 128kb)


(module-run 'network)
