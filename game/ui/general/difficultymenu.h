
#pragma once

#include "framework/stage.h"

#include "forms/forms.h"

namespace OpenApoc
{

class DifficultyMenu : public Stage
{
  private:
	sp<Form> difficultymenuform;
	StageCmd stageCmd;

  public:
	DifficultyMenu();
	~DifficultyMenu() override;
	// Stage control
	void Begin() override;
	void Pause() override;
	void Resume() override;
	void Finish() override;
	void EventOccurred(Event *e) override;
	void Update(StageCmd *const cmd) override;
	void Render() override;
	bool IsTransition() override;
};
}; // namespace OpenApoc
