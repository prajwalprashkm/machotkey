#ifndef SECURE_DIALOG_H
#define SECURE_DIALOG_H

#include <string>

bool show_dialog(const std::string& title, const std::string& description);

enum class RateApprovalChoice : int {
    Deny = 0,
    AllowOnce = 1,
    AllowAlways = 2
};

RateApprovalChoice show_rate_approval_dialog(const std::string& title, const std::string& description);

#endif