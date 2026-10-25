#pragma once
#include <string>
void i18n_load(const std::string &path);
const char *tr(const char *en);
#include <vector>
std::vector<std::string> i18n_list(const std::string &dir);
