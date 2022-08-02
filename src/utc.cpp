/**
 * @file utc.h
 * @author your Otto Pattemore https://github.com/OttoPattemore/shortwave-station-list-sdrpp/
 * @brief 
 * @version 0.1
 * @date 2022-08-02
 * 
 * @copyright Copyright (c) Otto Pattemore 2022
 * 
 */
#include "utc.h"
#include <chrono>
float getUTCTime()
{
    std::time_t now = std::time(0);
    std::tm *now_tm = std::gmtime(&now);
    return now_tm->tm_min + (now_tm->tm_hour * 100);
}
int getUTCHour()
{
    std::time_t now = std::time(0);
    std::tm *now_tm = std::gmtime(&now);
    return now_tm->tm_hour;
}
int getUTCMin()
{
    std::time_t now = std::time(0);
    std::tm *now_tm = std::gmtime(&now);
    return now_tm->tm_min;
}
