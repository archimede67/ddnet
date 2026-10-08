#include "map_diff_snapshot.h"

#include <game/editor/mapitems/image.h>
#include <game/editor/mapitems/layer_group.h>
#include <game/editor/mapitems/layer_quads.h>
#include <game/editor/mapitems/layer_sounds.h>
#include <game/editor/mapitems/layer_speedup.h>
#include <game/editor/mapitems/layer_switch.h>
#include <game/editor/mapitems/layer_tele.h>
#include <game/editor/mapitems/layer_tune.h>
#include <game/editor/mapitems/map.h>
#include <game/editor/mapitems/sound.h>

#include <algorithm>
#include <limits>

namespace MapDiff
{
	namespace
	{
		void Add(TProperties &Properties, const std::string &Name, int64_t Value)
		{
			Properties.push_back({Name, std::to_string(Value)});
		}
		void Color(TProperties &Properties, const std::string &Name, const CColor &Value)
		{
			Add(Properties, Name + ".r", Value.r);
			Add(Properties, Name + ".g", Value.g);
			Add(Properties, Name + ".b", Value.b);
			Add(Properties, Name + ".a", Value.a);
		}
		void Point(TProperties &Properties, const std::string &Name, const CPoint &Value)
		{
			Add(Properties, Name + ".x (1/1024 unit)", Value.x);
			Add(Properties, Name + ".y (1/1024 unit)", Value.y);
		}
		void Reference(TProperties &Properties, std::vector<std::string> &Issues, std::vector<CReference> &References, EResourceKind Kind, const char *pName, int Index, const std::vector<CResource> &Resources)
		{
			References.push_back({Kind, Index});
			std::string Value;
			if(Index == -1)
				Value = "None";
			else if(Index < 0 || Index >= (int)Resources.size())
			{
				Value = "Invalid reference " + std::to_string(Index);
				Issues.push_back(std::string(pName) + ": " + Value);
			}
			else
			{
				const auto &Resource = Resources[Index];
				Value = Resource.m_ContentKey;
				if(Resource.m_Missing)
					Issues.push_back(std::string(pName) + ": missing content for \"" + Resource.m_Name + "\"");
			}
			Properties.push_back({pName, Value});
		}
		CObject Quad(const CQuad &Quad, const CSnapshot &Map, std::vector<std::string> &Issues)
		{
			CObject Object;
			Object.m_aBounds = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
			for(int i = 0; i < 5; ++i)
			{
				Point(Object.m_vProperties, "Point " + std::to_string(i), Quad.m_aPoints[i]);
				if(i < 4)
				{
					Object.m_aBounds[0] = std::min(Object.m_aBounds[0], Quad.m_aPoints[i].x / 1024.0f);
					Object.m_aBounds[1] = std::min(Object.m_aBounds[1], Quad.m_aPoints[i].y / 1024.0f);
					Object.m_aBounds[2] = std::max(Object.m_aBounds[2], Quad.m_aPoints[i].x / 1024.0f);
					Object.m_aBounds[3] = std::max(Object.m_aBounds[3], Quad.m_aPoints[i].y / 1024.0f);
					Color(Object.m_vProperties, "Color " + std::to_string(i), Quad.m_aColors[i]);
					Point(Object.m_vProperties, "Texcoord " + std::to_string(i), Quad.m_aTexcoords[i]);
				}
			}
			Reference(Object.m_vProperties, Issues, Object.m_vReferences, EResourceKind::ENVELOPE, "Position envelope", Quad.m_PosEnv, Map.m_vEnvelopes);
			Add(Object.m_vProperties, "Position envelope offset (ms)", Quad.m_PosEnvOffset);
			Reference(Object.m_vProperties, Issues, Object.m_vReferences, EResourceKind::ENVELOPE, "Color envelope", Quad.m_ColorEnv, Map.m_vEnvelopes);
			Add(Object.m_vProperties, "Color envelope offset (ms)", Quad.m_ColorEnvOffset);
			return Object;
		}
		CObject Sound(const CSoundSource &Source, const CSnapshot &Map, std::vector<std::string> &Issues)
		{
			CObject Object;
			Point(Object.m_vProperties, "Position", Source.m_Position);
			Add(Object.m_vProperties, "Loop", Source.m_Loop);
			Add(Object.m_vProperties, "Pan", Source.m_Pan);
			Add(Object.m_vProperties, "Time delay (s)", Source.m_TimeDelay);
			Add(Object.m_vProperties, "Falloff", Source.m_Falloff);
			Add(Object.m_vProperties, "Shape", Source.m_Shape.m_Type);
			float X = Source.m_Position.x / 1024.0f, Y = Source.m_Position.y / 1024.0f;
			float W = 16.0f, H = 16.0f;
			if(Source.m_Shape.m_Type == CSoundShape::SHAPE_RECTANGLE)
			{
				Add(Object.m_vProperties, "Width (1/1024 unit)", Source.m_Shape.m_Rectangle.m_Width);
				Add(Object.m_vProperties, "Height (1/1024 unit)", Source.m_Shape.m_Rectangle.m_Height);
				W = Source.m_Shape.m_Rectangle.m_Width / 2048.0f;
				H = Source.m_Shape.m_Rectangle.m_Height / 2048.0f;
			}
			else if(Source.m_Shape.m_Type == CSoundShape::SHAPE_CIRCLE)
			{
				Add(Object.m_vProperties, "Radius", Source.m_Shape.m_Circle.m_Radius);
				W = H = Source.m_Shape.m_Circle.m_Radius;
			}
			else
				Issues.push_back("Invalid sound shape " + std::to_string(Source.m_Shape.m_Type));
			Object.m_aBounds = {X - W, Y - H, X + W, Y + H};
			Reference(Object.m_vProperties, Issues, Object.m_vReferences, EResourceKind::ENVELOPE, "Position envelope", Source.m_PosEnv, Map.m_vEnvelopes);
			Add(Object.m_vProperties, "Position envelope offset (ms)", Source.m_PosEnvOffset);
			Reference(Object.m_vProperties, Issues, Object.m_vReferences, EResourceKind::ENVELOPE, "Sound envelope", Source.m_SoundEnv, Map.m_vEnvelopes);
			Add(Object.m_vProperties, "Sound envelope offset (ms)", Source.m_SoundEnvOffset);
			return Object;
		}
		struct CCancelled
		{
		};
	}
	bool Capture(const CEditorMap &Map, CSnapshot &Snapshot, const FCancel &Cancel)
	{
		CSnapshot Result;
		auto Check = [&]() {
			if(Cancel && Cancel())
				throw CCancelled();
		};
		try
		{
			Check();
			Result.m_vInfo = {{"Author", Map.m_MapInfo.m_aAuthor}, {"Version", Map.m_MapInfo.m_aVersion}, {"Credits", Map.m_MapInfo.m_aCredits}, {"License", Map.m_MapInfo.m_aLicense}};
			for(const auto &Setting : Map.m_vSettings)
				Result.m_vSettings.emplace_back(Setting.m_aCommand);
			for(const auto &pImage : Map.m_vpImages)
			{
				Check();
				CResource Resource;
				Resource.m_Name = pImage->m_aName;
				Add(Resource.m_vProperties, "Width", pImage->m_Width);
				Add(Resource.m_vProperties, "Height", pImage->m_Height);
				Add(Resource.m_vProperties, "External", pImage->m_External);
				Resource.m_Missing = pImage->m_pData == nullptr || pImage->m_Width == 0 || pImage->m_Height == 0;
				if(pImage->m_External)
					Resource.m_vProperties.push_back({"External image", pImage->m_aName});
				else
				{
					Add(Resource.m_vProperties, "Pixel format", pImage->m_Format);
					if(pImage->m_Format != CImageInfo::FORMAT_UNDEFINED && !Resource.m_Missing)
						Resource.m_vData.assign(pImage->m_pData, pImage->m_pData + pImage->DataSize());
					else
						Resource.m_Missing = true;
				}
				Resource.m_ContentKey = ResourceContentKey(Resource);
				Result.m_vImages.push_back(std::move(Resource));
			}
			for(const auto &pSound : Map.m_vpSounds)
			{
				Check();
				CResource Resource;
				Resource.m_Name = pSound->m_aName;
				Resource.m_Missing = pSound->m_pData == nullptr || pSound->m_DataSize == 0;
				if(!Resource.m_Missing)
				{
					const auto *pBytes = static_cast<const uint8_t *>(pSound->m_pData);
					Resource.m_vData.assign(pBytes, pBytes + pSound->m_DataSize);
				}
				Resource.m_ContentKey = ResourceContentKey(Resource);
				Result.m_vSounds.push_back(std::move(Resource));
			}
			for(const auto &pEnvelope : Map.m_vpEnvelopes)
			{
				Check();
				CResource Resource;
				Resource.m_Name = pEnvelope->m_aName;
				const int Channels = pEnvelope->Type() == CEnvelope::EType::POSITION ? 3 : pEnvelope->Type() == CEnvelope::EType::COLOR ? 4 :
																			  1;
				Add(Resource.m_vProperties, "Channels", Channels);
				Add(Resource.m_vProperties, "Synchronized", pEnvelope->m_Synchronized);
				for(size_t i = 0; i < pEnvelope->m_vPoints.size(); ++i)
				{
					Check();
					const auto &Point = pEnvelope->m_vPoints[i];
					const std::string Prefix = "Point " + std::to_string(i) + ": ";
					Add(Resource.m_vProperties, Prefix + "Time (ms)", Point.m_Time.GetInternal());
					Add(Resource.m_vProperties, Prefix + "Curve", Point.m_Curvetype);
					for(int c = 0; c < Channels; ++c)
					{
						const std::string Channel = Prefix + "Channel " + std::to_string(c) + " ";
						Add(Resource.m_vProperties, Channel + "value (1/1024)", Point.m_aValues[c]);
						if(Point.m_Curvetype == CURVETYPE_BEZIER)
						{
							Add(Resource.m_vProperties, Channel + "out dx (ms)", Point.m_Bezier.m_aOutTangentDeltaX[c].GetInternal());
							Add(Resource.m_vProperties, Channel + "out dy (1/1024)", Point.m_Bezier.m_aOutTangentDeltaY[c]);
						}
						if(i > 0 && pEnvelope->m_vPoints[i - 1].m_Curvetype == CURVETYPE_BEZIER)
						{
							Add(Resource.m_vProperties, Channel + "in dx (ms)", Point.m_Bezier.m_aInTangentDeltaX[c].GetInternal());
							Add(Resource.m_vProperties, Channel + "in dy (1/1024)", Point.m_Bezier.m_aInTangentDeltaY[c]);
						}
					}
				}
				Resource.m_ContentKey = ResourceContentKey(Resource);
				Result.m_vEnvelopes.push_back(std::move(Resource));
			}
			for(const auto &pGroup : Map.m_vpGroups)
			{
				Check();
				CGroup Group;
				Group.m_Name = pGroup->m_aName;
				Group.m_Game = pGroup->m_GameGroup;
				Add(Group.m_vProperties, "Offset X", pGroup->m_OffsetX);
				Add(Group.m_vProperties, "Offset Y", pGroup->m_OffsetY);
				Add(Group.m_vProperties, "Parallax X", pGroup->m_ParallaxX);
				Add(Group.m_vProperties, "Parallax Y", pGroup->m_ParallaxY);
				Add(Group.m_vProperties, "Use clipping", pGroup->m_UseClipping);
				Add(Group.m_vProperties, "Clip X", pGroup->m_ClipX);
				Add(Group.m_vProperties, "Clip Y", pGroup->m_ClipY);
				Add(Group.m_vProperties, "Clip width", pGroup->m_ClipW);
				Add(Group.m_vProperties, "Clip height", pGroup->m_ClipH);
				for(const auto &pLayer : pGroup->m_vpLayers)
				{
					Check();
					CLayer Layer;
					Layer.m_Name = pLayer->m_aName;
					Add(Layer.m_vProperties, "Flags", pLayer->m_Flags);
					if(pLayer->m_Type == LAYERTYPE_TILES)
					{
						const auto &Tiles = static_cast<const CLayerTiles &>(*pLayer);
						Layer.m_Kind = Tiles.m_HasTele ? ELayerKind::TELE : Tiles.m_HasSpeedup ? ELayerKind::SPEEDUP :
											    Tiles.m_HasFront           ? ELayerKind::FRONT :
											    Tiles.m_HasSwitch          ? ELayerKind::SWITCH :
											    Tiles.m_HasTune            ? ELayerKind::TUNE :
											    Tiles.m_HasGame            ? ELayerKind::GAME :
															 ELayerKind::TILES;
						Layer.m_Width = Tiles.m_Width;
						Layer.m_Height = Tiles.m_Height;
						Color(Layer.m_vProperties, "Color", Tiles.m_Color);
						Reference(Layer.m_vProperties, Layer.m_vIssues, Layer.m_vReferences, EResourceKind::IMAGE, "Image", Tiles.m_Image, Result.m_vImages);
						Reference(Layer.m_vProperties, Layer.m_vIssues, Layer.m_vReferences, EResourceKind::ENVELOPE, "Color envelope", Tiles.m_ColorEnv, Result.m_vEnvelopes);
						Add(Layer.m_vProperties, "Color envelope offset (ms)", Tiles.m_ColorEnvOffset);
						if(Layer.m_Kind == ELayerKind::TILES)
						{
							Add(Layer.m_vProperties, "Automapper configuration", Tiles.m_AutomapperConfig);
							Add(Layer.m_vProperties, "Automapper seed", Tiles.m_Seed);
							Add(Layer.m_vProperties, "Automatic automapper", Tiles.m_AutoAutomapper);
						}
						for(int y = 0; y < Tiles.m_Height; ++y)
						{
							Check();
							for(int x = 0; x < Tiles.m_Width; ++x)
							{
								const size_t i = (size_t)y * Tiles.m_Width + x;
								CTile Tile;
								if(Layer.m_Kind == ELayerKind::TELE)
								{
									const auto &T = static_cast<const CLayerTele &>(Tiles).m_pTeleTile[i];
									Tile.m_Index = T.m_Type;
									Tile.m_Number = T.m_Number;
								}
								else if(Layer.m_Kind == ELayerKind::SPEEDUP)
								{
									const auto &T = static_cast<const CLayerSpeedup &>(Tiles).m_pSpeedupTile[i];
									Tile.m_Index = T.m_Type;
									Tile.m_Force = T.m_Force;
									Tile.m_MaxSpeed = T.m_MaxSpeed;
									Tile.m_Angle = T.m_Angle;
								}
								else if(Layer.m_Kind == ELayerKind::SWITCH)
								{
									const auto &T = static_cast<const CLayerSwitch &>(Tiles).m_pSwitchTile[i];
									Tile.m_Index = T.m_Type;
									Tile.m_Number = T.m_Number;
									Tile.m_Delay = T.m_Delay;
									Tile.m_Flags = T.m_Flags & (TILEFLAG_XFLIP | TILEFLAG_YFLIP | TILEFLAG_ROTATE);
								}
								else if(Layer.m_Kind == ELayerKind::TUNE)
								{
									const auto &T = static_cast<const CLayerTune &>(Tiles).m_pTuneTile[i];
									Tile.m_Index = T.m_Type;
									Tile.m_Number = T.m_Number;
								}
								else
								{
									Tile.m_Index = Tiles.m_pTiles[i].m_Index;
									Tile.m_Flags = Tiles.m_pTiles[i].m_Flags & (TILEFLAG_XFLIP | TILEFLAG_YFLIP | TILEFLAG_ROTATE);
								}
								if(!(Tile == CTile{}))
									Layer.m_vCells.push_back({x, y, Tile});
							}
						}
					}
					else if(pLayer->m_Type == LAYERTYPE_QUADS)
					{
						Layer.m_Kind = ELayerKind::QUADS;
						const auto &Quads = static_cast<const CLayerQuads &>(*pLayer);
						Reference(Layer.m_vProperties, Layer.m_vIssues, Layer.m_vReferences, EResourceKind::IMAGE, "Image", Quads.m_Image, Result.m_vImages);
						for(const auto &Q : Quads.m_vQuads)
						{
							Check();
							Layer.m_vObjects.push_back(Quad(Q, Result, Layer.m_vIssues));
						}
					}
					else if(pLayer->m_Type == LAYERTYPE_SOUNDS)
					{
						Layer.m_Kind = ELayerKind::SOUNDS;
						const auto &Sounds = static_cast<const CLayerSounds &>(*pLayer);
						Reference(Layer.m_vProperties, Layer.m_vIssues, Layer.m_vReferences, EResourceKind::SOUND, "Sound", Sounds.m_Sound, Result.m_vSounds);
						for(const auto &S : Sounds.m_vSources)
						{
							Check();
							Layer.m_vObjects.push_back(Sound(S, Result, Layer.m_vIssues));
						}
					}
					else
					{
						Layer.m_Kind = ELayerKind::UNKNOWN;
						Layer.m_vIssues.push_back("Unsupported layer type " + std::to_string(pLayer->m_Type));
					}
					Group.m_vLayers.push_back(std::move(Layer));
				}
				Result.m_vGroups.push_back(std::move(Group));
			}
		}
		catch(const CCancelled &)
		{
			Snapshot = {};
			return false;
		}
		Snapshot = std::move(Result);
		return true;
	}
}
