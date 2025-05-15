#pragma once
#include <string>

#define GOOGLE_ACCOUNTS "https://accounts.google.com/o/oauth2/v2/auth"
#define CLIENT_ID "1066306325374-itr5ih1ivquo8hmi841ts7mumv2vn2k4.apps.googleusercontent.com"
#define CLIENT_SECRET "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"
#define GOOGLE_APIS "oauth2.googleapis.com"
#define MUTT_CONFIG ".mutt/accounts"
#define ACCESS_TOKEN "access_token"
#define REFRESH_TOKEN "refresh_token"

bool getAccess();
std::string getJson(std::string&&);
std::string getToken(std::string&&, const std::string&);
time_t getTime(std::string&&, const std::string&);
std::string urlEncode(const std::string&);


