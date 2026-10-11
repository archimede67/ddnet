#include <game/client/lineinput_history.h>

#include <gtest/gtest.h>

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

TEST(LineInputHistory, HiddenSubmissionCannotRestorePasswordAfterBecomingVisible)
{
	CLineInputHistory History;
	const CLineInputHistory::CState Empty{"", 0, 0, 0};
	const CLineInputHistory::CState Secret{"rcon password", 13, 13, 13};
	History.Record(Empty, Secret);
	History.SetEnabled(false);
	EXPECT_FALSE(History.Undo(Secret));
	// RCON clears the active hidden input after submission, then shows commands.
	History.Record(Secret, Empty);
	History.Clear();
	History.Record(Secret, Empty);
	EXPECT_FALSE(History.Undo(Empty));
	History.SetEnabled(true);
	EXPECT_FALSE(History.Undo(Empty));
	EXPECT_FALSE(History.Redo(Empty));
	const CLineInputHistory::CState Command{"status", 6, 6, 6};
	History.Record(Empty, Command);
	EXPECT_EQ(History.Undo(Command), Empty);
	EXPECT_EQ(History.Redo(Empty), Command);
}
