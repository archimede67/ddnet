
TEST_F(CEditorHistoryRuntime, AuthoritativeFieldAndOrdinaryToolExtensionExercise)
{
	ASSERT_FALSE(Map().m_vpGroups.empty());
	const auto Baseline = Snapshot();
	const auto Key = Timeline().History()->Revisions().front().m_PersistedKey;
	int ToolOwner = 0;
	// A new tool uses the ordinary lifecycle. No history class, inverse,
	// property tracker, capture mapping or restore mapping is added.
	const auto NewTool = [&](int Value) {
		return Timeline().Edit(&ToolOwner, "New group authoring property", editor_history::ECategory::MAP,
			[&] { Map().m_vpGroups.front()->m_ExtensionExerciseValue = Value; });
	};
	ASSERT_TRUE(NewTool(314));
	ASSERT_EQ(Timeline().History()->Revisions().size(), 2u);
	EXPECT_EQ(Map().m_vpGroups.front()->m_ExtensionExerciseValue, 314);
	EXPECT_EQ(Timeline().History()->Current().m_Groups.Read().front().Read().m_ExtensionExerciseValue, 314);
	EXPECT_EQ(Timeline().History()->Revisions().back().m_PersistedKey, Key);
	const auto Changed = Snapshot();
	ASSERT_TRUE(Timeline().Undo());
	ASSERT_TRUE(Timeline().PublishAtSafePoint());
	EXPECT_EQ(Map().m_vpGroups.front()->m_ExtensionExerciseValue, 0);
	EXPECT_TRUE(Snapshot() == Baseline);
	ASSERT_TRUE(Timeline().Begin(&ToolOwner, "Cancelled extension gesture", editor_history::ECategory::MAP));
	for(int Update = 1; Update <= 200; ++Update)
		ASSERT_TRUE(Timeline().Update(&ToolOwner, [&] { Map().m_vpGroups.front()->m_ExtensionExerciseValue = Update; }));
	ASSERT_TRUE(Timeline().Cancel(editor_history::EEditCancellation::ESCAPE));
	ASSERT_TRUE(Timeline().PublishAtSafePoint());
	EXPECT_TRUE(Snapshot() == Baseline);
	EXPECT_TRUE(Timeline().CanRedo());
	ASSERT_TRUE(Timeline().Redo());
	ASSERT_TRUE(Timeline().PublishAtSafePoint());
	EXPECT_EQ(Map().m_vpGroups.front()->m_ExtensionExerciseValue, 314);
	EXPECT_TRUE(Snapshot() == Changed);
	ASSERT_TRUE(NewTool(314));
	EXPECT_EQ(Timeline().History()->Revisions().size(), 2u);
}
