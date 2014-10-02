
#include "boot.h"
#include "../framework/framework.h"
#include "general/mainmenu.h"
#include "../transitions/transitions.h"
#include "resources/gamecore.h"

void BootUp::Begin()
{
	loadingimage = DATA->load_image( "UI/LOADING.PNG" );
	loadtime = 0;
	FRAMEWORK->Display_SetTitle("OpenApocalypse");
	loadingimageangle = new Angle();

	threadload = nullptr;
	threadload = al_create_thread( CreateGameCore, nullptr );
	if( threadload != nullptr )
	{
		al_start_thread( threadload );
	}
}

void BootUp::Pause()
{
}

void BootUp::Resume()
{
}

void BootUp::Finish()
{
}

void BootUp::EventOccurred(Event *e)
{
	if( e->Type == EVENT_KEY_DOWN )
	{
		if( e->Data.Keyboard.KeyCode == ALLEGRO_KEY_ESCAPE )
		{
			delete FRAMEWORK->ProgramStages->Pop();
		} else {
			loadtime = FRAMES_PER_SECOND;
		}
	}
}

void BootUp::Update()
{
	loadtime++;
	loadingimageangle->Add( 5 );

	if( threadload == nullptr && GAMECORE == nullptr )
	{
		CreateGameCore( nullptr, nullptr );
	}

	if( GAMECORE != nullptr && GAMECORE->Loaded && loadtime > FRAMES_PER_SECOND * 2 )
	{
		StartGame();
	}
}

void BootUp::Render()
{
	al_clear_to_color( al_map_rgb( 0, 0, 0 ) );
	loadingimage->drawRotated( 24, 24, FRAMEWORK->Display_GetWidth() - 50, FRAMEWORK->Display_GetHeight() - 50, loadingimageangle->ToRadians() );
}

void BootUp::StartGame()
{
	if( threadload != nullptr )
	{
		al_destroy_thread( threadload );
	}
	delete FRAMEWORK->ProgramStages->Pop();
	FRAMEWORK->ProgramStages->Push( new TransitionFadeIn( new MainMenu(), al_map_rgb( 0, 0, 0 ), FRAMES_PER_SECOND ) );
}

bool BootUp::IsTransition()
{
	return false;
}

void* BootUp::CreateGameCore(ALLEGRO_THREAD* thread, void* args)
{
	std::string ruleset(*FRAMEWORK->Settings->GetQuickStringValue( "GameRules", "XCOMAPOC.XML" ));
	std::string language(*FRAMEWORK->Settings->GetQuickStringValue( "Language", "en_gb" ));

	GameCore* c = new GameCore( ruleset, language );

	return nullptr;
}
