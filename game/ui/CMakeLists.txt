# project name, and type
PROJECT(OpenApoc_GameUI CXX C)

include(cotire)

# check cmake version
CMAKE_MINIMUM_REQUIRED(VERSION 3.1)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package (Threads REQUIRED)

set (GAMEUI_SOURCE_FILES
	base/basescreen.cpp
	base/basestage.cpp
	base/transactionscreen.cpp
	base/researchscreen.cpp
	base/researchselect.cpp
	base/vequipscreen.cpp
	
	battle/battlebriefing.cpp
	battle/battledebriefing.cpp
	battle/battleprestart.cpp
	battle/battleturnbasedconfirmbox.cpp
	
	city/basebuyscreen.cpp
	city/baseselectscreen.cpp
	city/buildingscreen.cpp
	city/alertscreen.cpp
	city/infiltrationscreen.cpp
	city/basedefensescreen.cpp
	city/scorescreen.cpp
	
	components/agentassignment.cpp
	components/basegraphics.cpp
	components/controlgenerator.cpp
	components/equipscreen.cpp
	components/locationscreen.cpp
	
	debugtools/debugmenu.cpp
	debugtools/formpreview.cpp
	debugtools/imagepreview.cpp
	
	general/aequipscreen.cpp
	general/difficultymenu.cpp
	general/ingameoptions.cpp
	general/loadingscreen.cpp
	general/mainmenu.cpp
	general/messagebox.cpp
	general/optionsmenu.cpp
	general/savemenu.cpp
	general/videoscreen.cpp
	general/messagelogscreen.cpp
	general/notificationscreen.cpp
	
	skirmish/skirmish.cpp
	skirmish/selectforces.cpp
	skirmish/mapselector.cpp
	
	tileview/battletileview.cpp
	tileview/battleview.cpp
	tileview/citytileview.cpp
	tileview/cityview.cpp
	tileview/tileview.cpp
	
	ufopaedia/ufopaediacategoryview.cpp
	ufopaedia/ufopaediaview.cpp
	
	boot.cpp)
source_group(gamestate\\sources FILES ${GAMEUI_SOURCE_FILES})
set (GAMEUI_HEADER_FILES
	
	base/basescreen.h
	base/basestage.h
	base/transactionscreen.h
	base/researchscreen.h
	base/researchselect.h
	base/vequipscreen.h
	
	battle/battlebriefing.h
	battle/battledebriefing.h
	battle/battleprestart.h
	battle/battleturnbasedconfirmbox.h
	
	city/basebuyscreen.h
	city/baseselectscreen.h
	city/buildingscreen.h
	city/alertscreen.h
	city/infiltrationscreen.h
	city/basedefensescreen.h
	city/scorescreen.h
	
	components/agentassignment.h
	components/basegraphics.h
	components/controlgenerator.h
	components/equipscreen.h
	components/locationscreen.h
	
	debugtools/debugmenu.h
	debugtools/formpreview.h
	debugtools/imagepreview.h
	
	general/aequipscreen.h
	general/difficultymenu.h
	general/ingameoptions.h
	general/loadingscreen.h
	general/mainmenu.h
	general/messagebox.h
	general/optionsmenu.h
	general/savemenu.h
	general/videoscreen.h
	general/messagelogscreen.h
	general/notificationscreen.h
	
	skirmish/skirmish.h
	skirmish/selectforces.h
	skirmish/mapselector.h
	
	tileview/battletileview.h
	tileview/battleview.h
	tileview/citytileview.h
	tileview/cityview.h
	tileview/tileview.h
	
	ufopaedia/ufopaediacategoryview.h
	ufopaedia/ufopaediaview.h
	
	boot.h)
source_group(gamestate\\headers FILES ${GAMEUI_HEADER_FILES})

list(APPEND ALL_SOURCE_FILES ${GAMEUI_SOURCE_FILES})
list(APPEND ALL_HEADER_FILES ${GAMEUI_HEADER_FILES})

add_library(OpenApoc_GameUI STATIC ${GAMEUI_SOURCE_FILES}
		${GAMEUI_HEADER_FILES})

target_link_libraries(OpenApoc_GameUI PUBLIC OpenApoc_Library)
target_link_libraries(OpenApoc_GameUI PUBLIC OpenApoc_Framework)
target_link_libraries(OpenApoc_GameUI PUBLIC OpenApoc_GameState)
target_link_libraries(OpenApoc_GameUI PUBLIC OpenApoc_Forms)

# version.h is used in the main menu, and that's generated in the bin dir
target_include_directories(OpenApoc_GameUI PRIVATE ${CMAKE_BINARY_DIR})

set_property(TARGET OpenApoc_GameUI PROPERTY CXX_STANDARD 11)

set_target_properties(OpenApoc_GameUI PROPERTIES COTIRE_CXX_PREFIX_HEADER_INIT
	"gameui_pch.h")

if(ENABLE_COTIRE)
	cotire(OpenApoc_GameUI)
endif()
