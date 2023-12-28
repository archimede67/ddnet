#include "editor.h"

#include <game/editor/mapitems/image.h>
#include <game/editor/mapitems/sound.h>

const ColorRGBA CEditor::ms_DefaultPropColor = ColorRGBA(1, 1, 1, 0.5f);

int CEditor::DoProperties(CUIRect *pToolbox, CProperty *pProps, int *pIDs, int *pNewVal, const std::vector<ColorRGBA> &vColors)
{
	auto [_, Value] = DoPropertiesWithState<int>(pToolbox, pProps, pIDs, pNewVal, vColors);
	return Value;
}

template<typename E>
SEditResult<E> CEditor::DoPropertiesWithState(CUIRect *pToolBox, CProperty *pProps, int *pIDs, int *pNewVal, const std::vector<ColorRGBA> &vColors)
{
	return DoPropertiesWithState<E>(pToolBox, pProps, pIDs, pNewVal, nullptr, vColors);
}

template<typename E>
SEditResult<E> CEditor::DoPropertiesWithState(CUIRect *pToolBox, CProperty *pProps, int *pIDs, int *pNewVal, bool *pEditedManual, const std::vector<ColorRGBA> &vColors)
{
	int Change = -1;
	EEditState State = EEditState::EDITING;
	bool Manual = false;

	static const char *s_pInputTooltip = "Use left mouse button to drag and change the value. Hold shift to be more precise. Use right mouse button to edit as text.";

	const auto &&ValueSelectorType = [](const CProperty &Property) {
		return Property.m_Mixed ? SEditorValueSelectorProps::VALUE_MIXED : SEditorValueSelectorProps::VALUE_NORMAL;
	};

	for(int i = 0; pProps[i].m_pName; i++)
	{
		const ColorRGBA *pColor = i >= (int)vColors.size() ? &ms_DefaultPropColor : &vColors[i];

		CUIRect Slot;
		pToolBox->HSplitTop(13.0f, &Slot, pToolBox);
		CUIRect Label, Shifter;
		Slot.VSplitMid(&Label, &Shifter);
		Shifter.HMargin(1.0f, &Shifter);
		UI()->DoLabel(&Label, pProps[i].m_pName, 10.0f, TEXTALIGN_ML);

		if(pProps[i].m_Type == PROPTYPE_INT_STEP)
		{
			CUIRect Inc, Dec;

			Shifter.VSplitRight(10.0f, &Shifter, &Inc);
			Shifter.VSplitLeft(10.0f, &Dec, &Shifter);

			SEditorValueSelectorProps Props;
			Props.m_Type = ValueSelectorType(pProps[i]);
			auto NewValueRes = UiDoValueSelector((char *)&pIDs[i], &Shifter, "", pProps[i].m_Value, pProps[i].m_Min, pProps[i].m_Max, 1, 1.0f, s_pInputTooltip, &Manual, Props, IGraphics::CORNER_NONE, pColor);
			int NewValue = NewValueRes.m_Value;
			if(NewValue != pProps[i].m_Value || NewValueRes.m_State != EEditState::EDITING)
			{
				if(pEditedManual)
					*pEditedManual = Manual;
				*pNewVal = NewValue;
				Change = i;
				State = NewValueRes.m_State;
			}
			if(DoButton_ButtonDec((char *)&pIDs[i] + 1, nullptr, 0, &Dec, 0, "Decrease"))
			{
				*pNewVal = pProps[i].m_Value - 1;
				Change = i;
				State = EEditState::ONE_GO;
			}
			if(DoButton_ButtonInc(((char *)&pIDs[i]) + 2, nullptr, 0, &Inc, 0, "Increase"))
			{
				*pNewVal = pProps[i].m_Value + 1;
				Change = i;
				State = EEditState::ONE_GO;
			}
		}
		else if(pProps[i].m_Type == PROPTYPE_BOOL)
		{
			CUIRect No, Yes;
			Shifter.VSplitMid(&No, &Yes);

			// Any value < 0 makes the input disabled
			// Any value > 1 makes it so neither Yes nor No are checked
			bool IsNo = pProps[i].m_Value == 0;
			bool IsYes = pProps[i].m_Value == 1;
			bool IsDisabled = pProps[i].m_Value < 0;

			if(DoButton_ButtonDec(&pIDs[i], "No", IsDisabled ? -1 : IsNo, &No, 0, ""))
			{
				*pNewVal = 0;
				Change = i;
				State = EEditState::ONE_GO;
			}
			if(DoButton_ButtonInc(((char *)&pIDs[i]) + 1, "Yes", IsDisabled ? -1 : IsYes, &Yes, 0, ""))
			{
				*pNewVal = 1;
				Change = i;
				State = EEditState::ONE_GO;
			}
		}
		else if(pProps[i].m_Type == PROPTYPE_INT_SCROLL)
		{
			SEditorValueSelectorProps Props;
			Props.m_Type = ValueSelectorType(pProps[i]);
			auto NewValueRes = UiDoValueSelector(&pIDs[i], &Shifter, "", pProps[i].m_Value, pProps[i].m_Min, pProps[i].m_Max, 1, 1.0f, s_pInputTooltip, &Manual, Props);
			int NewValue = NewValueRes.m_Value;
			if(NewValue != pProps[i].m_Value || NewValueRes.m_State != EEditState::EDITING)
			{
				if(pEditedManual)
					*pEditedManual = Manual;
				*pNewVal = NewValue;
				Change = i;
				State = NewValueRes.m_State;
			}
		}
		else if(pProps[i].m_Type == PROPTYPE_ANGLE_SCROLL)
		{
			CUIRect Inc, Dec;
			Shifter.VSplitRight(10.0f, &Shifter, &Inc);
			Shifter.VSplitLeft(10.0f, &Dec, &Shifter);
			const bool Shift = Input()->ShiftIsPressed();
			int Step = Shift ? 1 : 45;
			int Value = pProps[i].m_Value;

			SEditorValueSelectorProps Props;
			Props.m_Type = ValueSelectorType(pProps[i]);
			auto NewValueRes = UiDoValueSelector(&pIDs[i], &Shifter, "", Value, pProps[i].m_Min, pProps[i].m_Max, Shift ? 1 : 45, Shift ? 1.0f : 10.0f, s_pInputTooltip, &Manual, Props, IGraphics::CORNER_NONE);
			int NewValue = NewValueRes.m_Value;
			if(DoButton_ButtonDec(&pIDs[i] + 1, nullptr, 0, &Dec, 0, "Decrease"))
			{
				NewValue = (std::ceil((pProps[i].m_Value / (float)Step)) - 1) * Step;
				if(NewValue < 0)
					NewValue += 360;
				State = EEditState::ONE_GO;
			}
			if(DoButton_ButtonInc(&pIDs[i] + 2, nullptr, 0, &Inc, 0, "Increase"))
			{
				NewValue = (pProps[i].m_Value + Step) / Step * Step;
				State = EEditState::ONE_GO;
			}

			if(NewValue != pProps[i].m_Value || NewValueRes.m_State != EEditState::EDITING)
			{
				if(pEditedManual)
					*pEditedManual = Manual;
				*pNewVal = NewValue % 360;
				Change = i;
				State = NewValueRes.m_State;
			}
		}
		else if(pProps[i].m_Type == PROPTYPE_COLOR)
		{
			const auto &&SetColor = [&](ColorRGBA NewColor) {
				const int NewValue = NewColor.PackAlphaLast();
				if(NewValue != pProps[i].m_Value || m_ColorPickerPopupContext.m_State != EEditState::EDITING)
				{
					*pNewVal = NewValue;
					Change = i;
					State = m_ColorPickerPopupContext.m_State;
				}
			};
			DoColorPickerButton(&pIDs[i], &Shifter, ColorRGBA::UnpackAlphaLast<ColorRGBA>(pProps[i].m_Value), SetColor);
		}
		else if(pProps[i].m_Type == PROPTYPE_IMAGE)
		{
			const char *pName;
			if(pProps[i].m_Mixed)
				pName = "Mixed";
			else if(pProps[i].m_Value < 0)
				pName = "None";
			else
				pName = m_Map.m_vpImages[pProps[i].m_Value]->m_aName;

			if(DoButton_Ex(&pIDs[i], pName, 0, &Shifter, 0, nullptr, IGraphics::CORNER_ALL))
				PopupSelectImageInvoke(pProps[i].m_Mixed ? -100 : pProps[i].m_Value, UI()->MouseX(), UI()->MouseY());

			int r = PopupSelectImageResult();
			if(r >= -1)
			{
				*pNewVal = r;
				Change = i;
				State = EEditState::ONE_GO;
			}
		}
		else if(pProps[i].m_Type == PROPTYPE_SHIFT)
		{
			CUIRect Left, Right, Up, Down;
			Shifter.VSplitMid(&Left, &Up, 2.0f);
			Left.VSplitLeft(10.0f, &Left, &Shifter);
			Shifter.VSplitRight(10.0f, &Shifter, &Right);
			Shifter.Draw(ColorRGBA(1, 1, 1, 0.5f), 0, 0.0f);
			UI()->DoLabel(&Shifter, "X", 10.0f, TEXTALIGN_MC);
			Up.VSplitLeft(10.0f, &Up, &Shifter);
			Shifter.VSplitRight(10.0f, &Shifter, &Down);
			Shifter.Draw(ColorRGBA(1, 1, 1, 0.5f), 0, 0.0f);
			UI()->DoLabel(&Shifter, "Y", 10.0f, TEXTALIGN_MC);
			if(DoButton_ButtonDec(&pIDs[i], "-", 0, &Left, 0, "Left"))
			{
				*pNewVal = DIRECTION_LEFT;
				Change = i;
				State = EEditState::ONE_GO;
			}
			if(DoButton_ButtonInc(((char *)&pIDs[i]) + 3, "+", 0, &Right, 0, "Right"))
			{
				*pNewVal = DIRECTION_RIGHT;
				Change = i;
				State = EEditState::ONE_GO;
			}
			if(DoButton_ButtonDec(((char *)&pIDs[i]) + 1, "-", 0, &Up, 0, "Up"))
			{
				*pNewVal = DIRECTION_UP;
				Change = i;
				State = EEditState::ONE_GO;
			}
			if(DoButton_ButtonInc(((char *)&pIDs[i]) + 2, "+", 0, &Down, 0, "Down"))
			{
				*pNewVal = DIRECTION_DOWN;
				Change = i;
				State = EEditState::ONE_GO;
			}
		}
		else if(pProps[i].m_Type == PROPTYPE_SOUND)
		{
			const char *pName;
			if(pProps[i].m_Mixed)
				pName = "Mixed";
			else if(pProps[i].m_Value < 0)
				pName = "None";
			else
				pName = m_Map.m_vpSounds[pProps[i].m_Value]->m_aName;

			if(DoButton_Ex(&pIDs[i], pName, 0, &Shifter, 0, nullptr, IGraphics::CORNER_ALL))
				PopupSelectSoundInvoke(pProps[i].m_Value, UI()->MouseX(), UI()->MouseY());

			int r = PopupSelectSoundResult();
			if(r >= -1)
			{
				*pNewVal = r;
				Change = i;
				State = EEditState::ONE_GO;
			}
		}
		else if(pProps[i].m_Type == PROPTYPE_AUTOMAPPER)
		{
			const char *pName;
			if(pProps[i].m_Mixed)
				pName = "Mixed";
			else if(pProps[i].m_Value < 0 || pProps[i].m_Min < 0 || pProps[i].m_Min >= (int)m_Map.m_vpImages.size())
				pName = "None";
			else
				pName = m_Map.m_vpImages[pProps[i].m_Min]->m_AutoMapper.GetConfigName(pProps[i].m_Value);

			if(DoButton_Ex(&pIDs[i], pName, 0, &Shifter, 0, nullptr, IGraphics::CORNER_ALL))
				PopupSelectConfigAutoMapInvoke(pProps[i].m_Value, UI()->MouseX(), UI()->MouseY());

			int r = PopupSelectConfigAutoMapResult();
			if(r >= -1)
			{
				*pNewVal = r;
				Change = i;
				State = EEditState::ONE_GO;
			}
		}
		else if(pProps[i].m_Type == PROPTYPE_ENVELOPE)
		{
			CUIRect Inc, Dec;
			char aBuf[8];
			int CurValue = pProps[i].m_Value;

			Shifter.VSplitRight(10.0f, &Shifter, &Inc);
			Shifter.VSplitLeft(10.0f, &Dec, &Shifter);

			if(CurValue <= 0)
				str_copy(aBuf, "None:");
			else if(m_Map.m_vpEnvelopes[CurValue - 1]->m_aName[0])
			{
				str_format(aBuf, sizeof(aBuf), "%s:", m_Map.m_vpEnvelopes[CurValue - 1]->m_aName);
				if(!str_endswith(aBuf, ":"))
				{
					aBuf[sizeof(aBuf) - 2] = ':';
					aBuf[sizeof(aBuf) - 1] = '\0';
				}
			}
			else
				aBuf[0] = '\0';

			SEditorValueSelectorProps Props;
			Props.m_Type = ValueSelectorType(pProps[i]);
			auto NewValueRes = UiDoValueSelector((char *)&pIDs[i], &Shifter, aBuf, CurValue, 0, m_Map.m_vpEnvelopes.size(), 1, 1.0f, "Set envelope", &Manual, Props, IGraphics::CORNER_NONE);
			int NewVal = NewValueRes.m_Value;
			if(NewVal != CurValue || NewValueRes.m_State != EEditState::EDITING)
			{
				if(pEditedManual)
					*pEditedManual = Manual;
				*pNewVal = NewVal;
				Change = i;
				State = NewValueRes.m_State;
			}

			if(DoButton_ButtonDec((char *)&pIDs[i] + 1, nullptr, 0, &Dec, 0, "Previous envelope"))
			{
				*pNewVal = pProps[i].m_Value - 1;
				Change = i;
				State = EEditState::ONE_GO;
			}
			if(DoButton_ButtonInc(((char *)&pIDs[i]) + 2, nullptr, 0, &Inc, 0, "Next envelope"))
			{
				*pNewVal = pProps[i].m_Value + 1;
				Change = i;
				State = EEditState::ONE_GO;
			}
		}
	}

	return SEditResult<E>{State, static_cast<E>(Change)};
}

template SEditResult<ECircleShapeProp> CEditor::DoPropertiesWithState(CUIRect *, CProperty *, int *, int *, const std::vector<ColorRGBA> &);
template SEditResult<ERectangleShapeProp> CEditor::DoPropertiesWithState(CUIRect *, CProperty *, int *, int *, const std::vector<ColorRGBA> &);
template SEditResult<EGroupProp> CEditor::DoPropertiesWithState(CUIRect *, CProperty *, int *, int *, const std::vector<ColorRGBA> &);
template SEditResult<ELayerProp> CEditor::DoPropertiesWithState(CUIRect *, CProperty *, int *, int *, const std::vector<ColorRGBA> &);
template SEditResult<ELayerQuadsProp> CEditor::DoPropertiesWithState(CUIRect *, CProperty *, int *, int *, const std::vector<ColorRGBA> &);
template SEditResult<ETilesProp> CEditor::DoPropertiesWithState(CUIRect *, CProperty *, int *, int *, const std::vector<ColorRGBA> &);
template SEditResult<ETilesCommonProp> CEditor::DoPropertiesWithState(CUIRect *, CProperty *, int *, int *, const std::vector<ColorRGBA> &);
template SEditResult<ELayerSoundsProp> CEditor::DoPropertiesWithState(CUIRect *, CProperty *, int *, int *, const std::vector<ColorRGBA> &);
template SEditResult<EQuadProp> CEditor::DoPropertiesWithState(CUIRect *, CProperty *, int *, int *, const std::vector<ColorRGBA> &);
template SEditResult<EQuadPointProp> CEditor::DoPropertiesWithState(CUIRect *, CProperty *, int *, int *, const std::vector<ColorRGBA> &);
template SEditResult<ESoundProp> CEditor::DoPropertiesWithState(CUIRect *, CProperty *, int *, int *, const std::vector<ColorRGBA> &);
template SEditResult<ELayerCommonProp> CEditor::DoPropertiesWithState(CUIRect *, CProperty *, int *, int *, const std::vector<ColorRGBA> &);
