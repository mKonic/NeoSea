#include "core/vim.h"

#include <gtest/gtest.h>

using namespace neosea::vim;

TEST(Vim, WordsAsVimCountsThem)
{
    // w: past this word and the space after it
    EXPECT_EQ(wordSteps('w', "", "Holston climbed."), 8);
    // punctuation is a word of its own
    EXPECT_EQ(wordSteps('w', "Holston climbed", "... and"), 4);
    EXPECT_EQ(wordSteps('w', "", "end.Next"), 3); // the word stops where the punctuation starts
    EXPECT_EQ(wordSteps('w', "", "don’t go"), 6); // the apostrophe belongs to the word
    // off the paragraph's end: on to the next
    EXPECT_EQ(wordSteps('w', "the ", "end"), -1);
}

TEST(Vim, BackAndToTheEnd)
{
    EXPECT_EQ(wordSteps('b', "Holston climbed", " on"), 7);
    EXPECT_EQ(wordSteps('b', "Holston climbed ", "on"), 8); // spaces first, then the word
    EXPECT_EQ(wordSteps('b', "", "x"), -1);                  // at the paragraph's start: the one before
    EXPECT_EQ(wordSteps('e', "", "Holston climbed"), 6);     // to the n of Holston
    EXPECT_EQ(wordSteps('e', "Holsto", "n climbed"), 8);     // already at an end: the next word's
}
