#include "core/i18n.h"

#include "print.h"

#include <gtest/gtest.h>

using namespace neosea;

TEST(I18n, FallsBackToEnglishKey)
{
    I18n::setLocale("en", {}, {});
    EXPECT_EQ(t("Cancel"), "Cancel");
    EXPECT_EQ(t("Chapter {n}", {{"n", 3}}), "Chapter 3");
}

TEST(I18n, LoadsNeoLocalesWithPlurals)
{
    I18n::load(NEOSEA_SOURCE_RESOURCES "/locales", "fr");
    EXPECT_EQ(I18n::locale(), "fr");
    EXPECT_EQ(t("{n} pages", {{"n", 1}}), "1 page");
    EXPECT_EQ(t("{n} pages", {{"n", 3}}), "3 pages");
    EXPECT_EQ(t("“{name}” · one book", {{"name", "X"}}), QString::fromUtf8("«\u00a0X\u00a0» · un livre"));
}

TEST(I18n, RegionalFallsBackToBase)
{
    EXPECT_EQ(I18n::resolve(NEOSEA_SOURCE_RESOURCES "/locales", "fr_CA"), "fr-CA");
    EXPECT_EQ(I18n::resolve(NEOSEA_SOURCE_RESOURCES "/locales", "de-AT"), "de");
    EXPECT_EQ(I18n::resolve(NEOSEA_SOURCE_RESOURCES "/locales", "ja"), "");
}

TEST(I18n, NumbersFollowTheLanguage)
{
    I18n::load(NEOSEA_SOURCE_RESOURCES "/locales", "en");
    EXPECT_EQ(I18n::formatNumber(1200), "1,200");
}
