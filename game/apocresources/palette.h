
#pragma once

#include "../../framework/includes.h"
#include "../../framework/framework.h"
#include "../../library/colour.h"

class Palette
{
	private:
		std::unique_ptr<Colour[]> colours;

	public:
		Palette( std::string Filename );
		~Palette();

		Colour &GetColour(int Index);
		void SetColour(int Index, Colour &Col);

		void DumpPalette( std::string Filename );
};

