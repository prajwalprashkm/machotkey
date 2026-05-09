#pragma once
#include <string>

std::string get_app_bundle_path();
const char* get_macro_runner_path();
std::string open_project_dialog();
bool prompt_quick_script_input(std::string& out_filename, std::string& out_code);