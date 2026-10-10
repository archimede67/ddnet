#include <game/client/lineinput_history.h>
#include <game/client/number_input.h>

#include <gtest/gtest.h>

TEST(NumberInput, CompleteDraftsAcceptSignsWhitespaceAndHexadecimal)
{
	EXPECT_EQ(ParseIntegerDraft("128", 10), 128);
	EXPECT_EQ(ParseIntegerDraft(" \t+128\r\n", 10), 128);
	EXPECT_EQ(ParseIntegerDraft("-128", 10), -128);
	EXPECT_EQ(ParseIntegerDraft("-0", 10), 0);
	EXPECT_EQ(ParseIntegerDraft("0xabcdef", 16), 0xabcdef);
	EXPECT_EQ(ParseIntegerDraft("-0XFF", 16), -255);
	EXPECT_EQ(ParseIntegerDraft("DEAD", 16), 0xdead);
	EXPECT_EQ(ParseIntegerDraft("9223372036854775807", 10), std::numeric_limits<int64_t>::max());
	EXPECT_EQ(ParseIntegerDraft("-9223372036854775808", 10), std::numeric_limits<int64_t>::min());
}

TEST(NumberInput, IncompleteInvalidAndOverflowingDraftsDoNotApplyPartialNumbers)
{
	for(const char *pDraft : {"", " ", "-", "+", "1e", "128oops", "12.5", "1 2", "--1", "+-1", "0xFF", "9223372036854775808", "-9223372036854775809", "18446744073709551616"})
		EXPECT_FALSE(ParseIntegerDraft(pDraft, 10)) << pDraft;
	for(const char *pDraft : {"0x", "-0x", "FFgarbage", "8000000000000000", "-8000000000000001"})
		EXPECT_FALSE(ParseIntegerDraft(pDraft, 16)) << pDraft;
	EXPECT_FALSE(ParseIntegerDraft("128", 2));
	EXPECT_EQ(ParseIntegerDraft("-8000000000000000", 16), std::numeric_limits<int64_t>::min());
}

TEST(LineInputHistory, TextAndSelectionUndoLocallyAndRedoExactly)
{
	CLineInputHistory History;
	const CLineInputHistory::CState Before{"name", 4, 0, 4};
	const CLineInputHistory::CState After{"new name", 8, 8, 8};
	History.Record(Before, After);
	EXPECT_EQ(History.Undo(After), Before);
	EXPECT_EQ(History.Redo(Before), After);
	EXPECT_FALSE(History.Redo(After));
}

TEST(LineInputHistory, EmptyAndUnchangedEditsPreserveTheLocalRedoBranch)
{
	CLineInputHistory History;
	const CLineInputHistory::CState Empty{"", 0, 0, 0};
	const CLineInputHistory::CState First{"1", 1, 1, 1};
	const CLineInputHistory::CState Second{"12", 2, 2, 2};
	EXPECT_FALSE(History.Undo(Empty));
	EXPECT_FALSE(History.Redo(Empty));
	History.Record(Empty, First);
	History.Record(First, Second);
	ASSERT_EQ(History.Undo(Second), First);
	History.Record(First, First);
	EXPECT_EQ(History.Redo(First), Second);
}

TEST(LineInputHistory, NewInputBranchesAfterLocalUndoAndExternalChangesResetIt)
{
	CLineInputHistory History;
	const CLineInputHistory::CState A{"a", 1, 1, 1};
	const CLineInputHistory::CState B{"ab", 2, 2, 2};
	const CLineInputHistory::CState C{"ac", 2, 2, 2};
	const CLineInputHistory::CState External{"replacement", 11, 11, 11};
	History.Record(A, B);
	ASSERT_EQ(History.Undo(B), A);
	History.Record(A, C);
	EXPECT_FALSE(History.Redo(C));
	EXPECT_EQ(History.Undo(C), A);
	EXPECT_FALSE(History.Undo(External));
	EXPECT_FALSE(History.Redo(External));
	History.Record(External, B);
	History.Clear();
	EXPECT_FALSE(History.Undo(B));
}

TEST(LineInputHistory, UnicodePasteRestoresByteOffsetsAndSelectionBeforeDeletion)
{
	CLineInputHistory History;
	const CLineInputHistory::CState Before{"aβい🐘", 10, 1, 10};
	const CLineInputHistory::CState Deleted{"a", 1, 1, 1};
	const CLineInputHistory::CState Pasted{"a你好", 7, 7, 7};
	History.Record(Before, Deleted);
	History.Record(Deleted, Pasted);
	EXPECT_EQ(History.Undo(Pasted), Deleted);
	EXPECT_EQ(History.Undo(Deleted), Before);
	EXPECT_FALSE(History.Undo(Before));
	EXPECT_EQ(History.Redo(Before), Deleted);
	EXPECT_EQ(History.Redo(Deleted), Pasted);
}

TEST(LineInputHistory, RetentionRemainsBoundedAndKeepsTheNewestEdit)
{
	CLineInputHistory History;
	CLineInputHistory::CState Current{"", 0, 0, 0};
	for(int Index = 0; Index < 300; ++Index)
	{
		auto Next = Current;
		Next.m_Text += 'x';
		Next.m_Cursor = Next.m_SelectionStart = Next.m_SelectionEnd = Next.m_Text.size();
		History.Record(Current, Next);
		Current = Next;
	}
	int UndoCount = 0;
	while(const auto Previous = History.Undo(Current))
	{
		EXPECT_EQ(Previous->m_Text.size() + 1, Current.m_Text.size());
		Current = *Previous;
		++UndoCount;
	}
	EXPECT_EQ(UndoCount, 127);
	EXPECT_EQ(Current.m_Text.size(), 173u);
}

TEST(LineInputNumber, CompleteFiniteFloatDraft)
{
	EXPECT_EQ(ParseFloatDraft("  +1.25e2 "), 125.0f);
	EXPECT_EQ(ParseFloatDraft("-.5"), -0.5f);
	for(const char *pInvalid : {"", "+", "-", ".", "1e", "1e+", "12junk", "1 2", "+-1", "++1", "nan", "inf", "1e100"})
		EXPECT_FALSE(ParseFloatDraft(pInvalid)) << pInvalid;
}

TEST(LineInputNumber, FloatDraftIsLocaleStableAndRejectsUnderflowOrTrailingSyntax)
{
	class CCommaDecimal : public std::numpunct<char>
	{
		char do_decimal_point() const override { return ','; }
	};
	const auto Previous = std::locale();
	std::locale::global(std::locale(Previous, new CCommaDecimal));
	EXPECT_EQ(ParseFloatDraft("1.25"), 1.25f);
	EXPECT_FALSE(ParseFloatDraft("1,25"));
	std::locale::global(Previous);
	EXPECT_EQ(ParseFloatDraft("\t-1.25E-2\r\n"), -0.0125f);
	EXPECT_EQ(ParseFloatDraft("1."), 1.0f);
	EXPECT_EQ(ParseFloatDraft("0e100"), 0.0f);
	const auto NegativeZero = ParseFloatDraft("-0.0");
	ASSERT_TRUE(NegativeZero);
	EXPECT_TRUE(std::signbit(*NegativeZero));
	const auto Subnormal = ParseFloatDraft("1.401298464324817e-45");
	ASSERT_TRUE(Subnormal);
	EXPECT_EQ(*Subnormal, std::numeric_limits<float>::denorm_min());
	EXPECT_EQ(ParseFloatDraft("3.4028234663852886e38"), std::numeric_limits<float>::max());
	EXPECT_EQ(ParseFloatDraft("3.4028235e38"), std::numeric_limits<float>::max());
	for(const char *pInvalid : {"0x1p2", "0x1", "1e-100", "-1e-100", "3.5e38", "1.2.3", "1.5x", "1,5", "\v1", "1\f", "NaN", "-Infinity"})
		EXPECT_FALSE(ParseFloatDraft(pInvalid)) << pInvalid;
	EXPECT_FALSE(ParseFloatDraft(std::string_view("1\0x", 3)));
}
