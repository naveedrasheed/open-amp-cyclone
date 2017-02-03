
if [ "$1" == "-c" ]; then
	echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	echo Cleanup OPENAMP library for baremetal and Linux
	echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	make clean
	
	make -f libs/system/cyclone_v_soc_dk/linux/make clean
		
	cd apps
	
	make OS=baremetal PLAT=cyclone_v_soc_dk ROLE=remote clean
	
	make OS=baremetal PLAT=cyclone_v_soc_dk ROLE=master clean

	make clean_linux_remote OS=baremetal PLAT=cyclone_v_soc_dk ROLE=master
		
	cd firmware 
	
	find . -name "firmware" -delete
	    
	cd ../..
	
else

	echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	echo Build OPENAMP library for remote baremetal and Master Linux
	echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

	echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	echo Cleaning open AMP components..
	echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

	make clean

    	echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	echo Building open AMP components..
	echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

	make OS=baremetal PLAT=cyclone_v_soc_dk ROLE=remote

    	echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	echo Build remote baremetal applications
    	echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

	cd apps

	echo ~~~~~~~~~~~~~~~~~~~~~~~
	echo Cleaning applications..
	echo ~~~~~~~~~~~~~~~~~~~~~~~

	make OS=baremetal PLAT=cyclone_v_soc_dk ROLE=remote clean

    	echo ~~~~~~~~~~~~~~~~~~~~~~~
	echo Building applications..
	echo ~~~~~~~~~~~~~~~~~~~~~~~

	make OS=baremetal PLAT=cyclone_v_soc_dk ROLE=remote

	cd ..

	echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	echo Cleaning Linux Bootstrap
	echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	make -f libs/system/cyclone_v_soc_dk/linux/make clean
	
	echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	echo Building Linux Bootstrap
	echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	make -f libs/system/cyclone_v_soc_dk/linux/make
	
	echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	echo Build OPENAMP library for master baremetal and remote Linux
	echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	echo Cleaning open AMP components..
	echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	make clean

    echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	echo Building open AMP components..
	echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	make OS=baremetal PLAT=cyclone_v_soc_dk ROLE=master LINUXREMOTE=1

	# Build baremetal master with linux remote
	cd apps
    
    echo ~~~~~~~~~~~~~~~~~~~~~~~
	echo Cleaning applications..
	echo ~~~~~~~~~~~~~~~~~~~~~~~
	
	make clean_linux_remote OS=baremetal PLAT=cyclone_v_soc_dk ROLE=master
    
    
    echo ~~~~~~~~~~~~~~~~~~~~~~~
	echo Building applications..
	echo ~~~~~~~~~~~~~~~~~~~~~~~
	make linux_remote OS=baremetal PLAT=cyclone_v_soc_dk ROLE=master

	cd ..
	
fi

