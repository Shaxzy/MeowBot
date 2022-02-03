void T::Walkbot()
{
	ImGuiStyle& style = ImGui::GetStyle();

	ImGui::BeginChild(XorStr("walkbot"), ImVec2(), true);
	{
		ImGui::TextUnformatted(XorStr("walkbot"));
		ImGui::Checkbox(XorStr("enabled"), &C::Get<bool>(Vars.bSkinChanger));
    
		ImGui::TextUnformatted(XorStr("MeowBot"));
		if (ImGui::Checkbox("enabled ", &C::Get<bool>(Vars.bWalkbot)))
			CWalkBot::Get().Reset();

		ImGui::Checkbox("humanize", &C::Get<bool>(Vars.bWalkbotHumanize));
		ImGui::Checkbox("visualize", &C::Get<bool>(Vars.bWalkbotVisualize));
		ImGui::Checkbox("auto optimize", &C::Get<bool>(Vars.bWalkbotAutoOptimize));
		ImGui::Checkbox("look at point", &C::Get<bool>(Vars.bWalkbotLookAtPoint));

		ImGui::EndChild();
	}
}
