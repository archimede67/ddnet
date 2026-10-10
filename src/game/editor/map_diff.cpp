#include "map_diff.h"

#include <base/hash.h>
#include <base/hash_ctxt.h>
#include <base/str.h>

#include <algorithm>
#include <deque>
#include <map>
#include <numeric>
#include <sstream>
#include <tuple>

namespace MapDiff
{
	namespace
	{
		struct CCancelled
		{
		};
		class CCheck
		{
			const FCancel &m_Cancel;
			size_t m_Count = 0;

		public:
			explicit CCheck(const FCancel &Cancel) :
				m_Cancel(Cancel) {}
			void Now()
			{
				if(m_Cancel && m_Cancel())
					throw CCancelled();
			}
			void Tick()
			{
				if((m_Count++ % 1024) == 0)
					Now();
			}
		};
		void InlineSettings(CSettingRow &Row, const std::string &Before, const std::string &After, size_t &Budget, CCheck &Check)
		{
			size_t Prefix = 0, EndA = Before.size(), EndB = After.size();
			while(Prefix < EndA && Prefix < EndB && Before[Prefix] == After[Prefix])
			{
				Check.Tick();
				++Prefix;
			}
			auto Continuation = [](char Byte) { return (static_cast<unsigned char>(Byte) & 0xc0) == 0x80; };
			// A byte prefix can end in the middle of a different Unicode codepoint.
			while(Prefix && ((Prefix < EndA && Continuation(Before[Prefix])) || (Prefix < EndB && Continuation(After[Prefix]))))
				--Prefix;
			while(EndA > Prefix && EndB > Prefix && Before[EndA - 1] == After[EndB - 1])
			{
				Check.Tick();
				--EndA;
				--EndB;
			}
			while((EndA < Before.size() && Continuation(Before[EndA])) || (EndB < After.size() && Continuation(After[EndB])))
			{
				++EndA;
				++EndB;
			}
			auto Add = [&](int Side, size_t Start, size_t End) {
				if(Start == End)
					return;
				auto &Ranges = Row.m_avChanged[Side];
				if(!Ranges.empty() && Ranges.back().m_End == (int)Start)
					Ranges.back().m_End = End;
				else
					Ranges.push_back({(int)Start, (int)End});
			};
			// Linear trimming is proportional to input bytes. Quadratic alignment is
			// capped both per command and for the entire settings result, using byte
			// counts as a conservative upper bound on decoded codepoint counts.
			const size_t BytesA = EndA - Prefix, BytesB = EndB - Prefix;
			const size_t Limit = std::min(Budget, size_t{64 * 1024});
			if(!BytesA || !BytesB || BytesA + 1 > Limit / (BytesB + 1))
			{
				Add(0, Prefix, EndA);
				Add(1, Prefix, EndB);
				return;
			}
			struct CUnit
			{
				int m_Codepoint;
				size_t m_Byte;
			};
			auto Decode = [&](const std::string &Text, size_t End) {
				std::vector<CUnit> Units;
				const char *pText = Text.c_str() + Prefix;
				while(pText < Text.c_str() + End)
				{
					Check.Tick();
					const size_t Byte = pText - Text.c_str();
					int Codepoint = str_utf8_decode(&pText);
					if(Codepoint < 0)
					{
						// The decoder may consume several malformed bytes. Preserve
						// each byte's identity instead of equating their leading byte.
						Codepoint = -1 - static_cast<unsigned char>(Text[Byte]);
						pText = Text.c_str() + Byte + 1;
					}
					Units.push_back({Codepoint, Byte});
				}
				Units.push_back({0, End});
				return Units;
			};
			const auto A = Decode(Before, EndA), B = Decode(After, EndB);
			const size_t N = A.size() - 1, M = B.size() - 1;
			Budget -= (N + 1) * (M + 1);
			std::vector<unsigned> Lcs((N + 1) * (M + 1));
			auto Cell = [&](size_t X, size_t Y) -> unsigned & { return Lcs[X * (M + 1) + Y]; };
			for(size_t X = N; X-- > 0;)
				for(size_t Y = M; Y-- > 0;)
				{
					Check.Tick();
					Cell(X, Y) = A[X].m_Codepoint == B[Y].m_Codepoint ? 1 + Cell(X + 1, Y + 1) : std::max(Cell(X + 1, Y), Cell(X, Y + 1));
				}
			size_t X = 0, Y = 0;
			while(X < N || Y < M)
			{
				Check.Tick();
				if(X < N && Y < M && A[X].m_Codepoint == B[Y].m_Codepoint)
				{
					++X;
					++Y;
				}
				else if(X < N && (Y == M || Cell(X + 1, Y) >= Cell(X, Y + 1)))
				{
					Add(0, A[X].m_Byte, A[X + 1].m_Byte);
					++X;
				}
				else
				{
					Add(1, B[Y].m_Byte, B[Y + 1].m_Byte);
					++Y;
				}
			}
		}
		void AlignSettings(CEntry &Entry, const std::vector<std::string> &Before, const std::vector<std::string> &After, CCheck &Check)
		{
			size_t InlineBudget = 1024 * 1024;
			const std::string Empty;
			auto Row = [&](int A, int B) {
				Check.Tick();
				CSettingRow Row{A, B, -1};
				if(A < 0 || B < 0 || Before[A] != After[B])
				{
					Row.m_Property = Entry.m_vProperties.size();
					Entry.m_vProperties.push_back({"Command " + (A < 0 ? std::string("-") : std::to_string(A + 1)) + " / " + (B < 0 ? std::string("-") : std::to_string(B + 1)), A < 0 ? "(absent)" : Before[A], B < 0 ? "(absent)" : After[B]});
					InlineSettings(Row, A < 0 ? Empty : Before[A], B < 0 ? Empty : After[B], InlineBudget, Check);
				}
				Entry.m_vSettings.push_back(std::move(Row));
			};
			size_t Prefix = 0, EndA = Before.size(), EndB = After.size();
			while(Prefix < EndA && Prefix < EndB && Before[Prefix] == After[Prefix])
			{
				Row(Prefix, Prefix);
				++Prefix;
			}
			while(EndA > Prefix && EndB > Prefix && Before[EndA - 1] == After[EndB - 1])
			{
				Check.Tick();
				--EndA;
				--EndB;
			}
			auto Span = [&](size_t A, size_t LimitA, size_t B, size_t LimitB) {
				while(A < LimitA || B < LimitB)
					Row(A < LimitA ? (int)A++ : -1, B < LimitB ? (int)B++ : -1);
			};
			const size_t N = EndA - Prefix, M = EndB - Prefix;
			// At most 1M cells (4 MiB) and comparisons, regardless of source length.
			constexpr size_t MaxCells = 1024 * 1024;
			if(N && M && N + 1 <= MaxCells / (M + 1))
			{
				std::vector<unsigned> vLcs((N + 1) * (M + 1));
				auto Cell = [&](size_t A, size_t B) -> unsigned & { return vLcs[A * (M + 1) + B]; };
				for(size_t A = N; A-- > 0;)
					for(size_t B = M; B-- > 0;)
					{
						Check.Tick();
						Cell(A, B) = Before[Prefix + A] == After[Prefix + B] ? 1 + Cell(A + 1, B + 1) : std::max(Cell(A + 1, B), Cell(A, B + 1));
					}
				size_t A = 0, B = 0, StartA = 0, StartB = 0;
				while(A < N && B < M)
				{
					Check.Tick();
					if(Before[Prefix + A] == After[Prefix + B])
					{
						Span(Prefix + StartA, Prefix + A, Prefix + StartB, Prefix + B);
						Row(Prefix + A++, Prefix + B++);
						StartA = A;
						StartB = B;
					}
					else if(Cell(A + 1, B) >= Cell(A, B + 1))
						++A;
					else
						++B;
				}
				Span(Prefix + StartA, EndA, Prefix + StartB, EndB);
			}
			else
			{
				Entry.m_SettingsConservative = N && M;
				Span(Prefix, EndA, Prefix, EndB);
			}
			while(EndA < Before.size())
				Row(EndA++, EndB++);
		}
		class CFingerprint
		{
			CCheck &m_Check;

		public:
			explicit CFingerprint(CCheck &Check) :
				m_Check(Check) {}
			uint64_t m_Value = 14695981039346656037ULL;
			void Add(uint64_t Value)
			{
				for(int i = 0; i < 8; ++i)
				{
					m_Value = (m_Value ^ (Value & 255)) * 1099511628211ULL;
					Value >>= 8;
				}
			}
			void Add(const std::string &Value)
			{
				Add(Value.size());
				for(size_t i = 0; i < Value.size(); ++i)
				{
					if((i % 65536) == 0)
						m_Check.Now();
					m_Value = (m_Value ^ (unsigned char)Value[i]) * 1099511628211ULL;
				}
			}
			void Add(const TProperties &Properties)
			{
				for(const auto &Property : Properties)
				{
					m_Check.Tick();
					Add(Property.m_Name);
					Add(Property.m_Value);
				}
			}
		};
		template<typename T>
		bool EqualBytes(const T &A, const T &B, CCheck &Check)
		{
			if(A.size() != B.size())
				return false;
			for(size_t Offset = 0; Offset < A.size(); Offset += 65536)
			{
				Check.Now();
				const size_t End = std::min(Offset + 65536, A.size());
				if(!std::equal(A.begin() + Offset, A.begin() + End, B.begin() + Offset))
					return false;
			}
			return true;
		}
		bool EqualProperties(const TProperties &A, const TProperties &B, CCheck &Check)
		{
			if(A.size() != B.size())
				return false;
			for(size_t i = 0; i < A.size(); ++i)
			{
				Check.Tick();
				if(!EqualBytes(A[i].m_Name, B[i].m_Name, Check) || !EqualBytes(A[i].m_Value, B[i].m_Value, Check))
					return false;
			}
			return true;
		}
		std::string ContentKey(const CResource &Resource, CCheck &Check)
		{
			auto Feed = [&](SHA256_CTX &Context, const auto &Bytes) {
				for(size_t Offset = 0; Offset < Bytes.size(); Offset += 65536)
				{
					Check.Now();
					sha256_update(&Context, Bytes.data() + Offset, std::min<size_t>(65536, Bytes.size() - Offset));
				}
			};
			SHA256_CTX DataContext, Context;
			sha256_init(&DataContext);
			Feed(DataContext, Resource.m_vData);
			char aDigest[SHA256_MAXSTRSIZE];
			sha256_str(sha256_finish(&DataContext), aDigest, sizeof(aDigest));
			sha256_init(&Context);
			for(const auto &Property : Resource.m_vProperties)
			{
				Check.Tick();
				Feed(Context, std::to_string(Property.m_Name.size()) + ":");
				Feed(Context, Property.m_Name);
				Feed(Context, std::to_string(Property.m_Value.size()) + ":");
				Feed(Context, Property.m_Value);
			}
			Feed(Context, std::string(aDigest));
			Feed(Context, std::string(Resource.m_Missing ? ":missing" : ":present"));
			sha256_str(sha256_finish(&Context), aDigest, sizeof(aDigest));
			return aDigest;
		}
		std::vector<CPropertyChange> Properties(const TProperties &Before, const TProperties &After, CCheck &Check);
		uint64_t Fingerprint(const CLayer &Layer, CCheck &Check)
		{
			Check.Tick();
			CFingerprint Hash(Check);
			Hash.Add((int)Layer.m_Kind);
			Hash.Add(Layer.m_vProperties);
			Hash.Add(Layer.m_Width);
			Hash.Add(Layer.m_Height);
			for(const auto &Cell : Layer.m_vCells)
			{
				Check.Tick();
				Hash.Add(Cell.m_X);
				Hash.Add(Cell.m_Y);
				const auto &T = Cell.m_Tile;
				Hash.Add(T.m_Index);
				Hash.Add(T.m_Flags);
				Hash.Add(T.m_Number);
				Hash.Add(T.m_Delay);
				Hash.Add(T.m_Force);
				Hash.Add(T.m_MaxSpeed);
				Hash.Add(T.m_Angle);
			}
			for(const auto &Object : Layer.m_vObjects)
			{
				Check.Tick();
				Hash.Add(Object.m_vProperties);
			}
			return Hash.m_Value;
		}
		bool Equal(const CLayer &A, const CLayer &B, CCheck &Check)
		{
			Check.Tick();
			if(A.m_Kind != B.m_Kind || A.m_Width != B.m_Width || A.m_Height != B.m_Height || !EqualProperties(A.m_vProperties, B.m_vProperties, Check) || A.m_vCells.size() != B.m_vCells.size() || A.m_vObjects.size() != B.m_vObjects.size())
				return false;
			for(size_t i = 0; i < A.m_vCells.size(); ++i)
			{
				Check.Tick();
				const auto &X = A.m_vCells[i];
				const auto &Y = B.m_vCells[i];
				if(X.m_X != Y.m_X || X.m_Y != Y.m_Y || !(X.m_Tile == Y.m_Tile))
					return false;
			}
			for(size_t i = 0; i < A.m_vObjects.size(); ++i)
			{
				Check.Tick();
				if(!EqualProperties(A.m_vObjects[i].m_vProperties, B.m_vObjects[i].m_vProperties, Check))
					return false;
			}
			return true;
		}
		struct CMatchItem
		{
			std::string m_Role;
			std::string m_Name;
			int m_Compatible = 0;
			uint64_t m_Hash = 0;
		};
		using CPair = CObjectChange;
		// Bounded matching: role, exact content, unique name, then only equal-sized
		// compatible remaining spans. Duplicate matches are explicitly uncertain.
		template<typename F>
		std::vector<CPair> Match(const std::vector<CMatchItem> &A, const std::vector<CMatchItem> &B, F Exact, CCheck &Check, bool Ordered = true)
		{
			std::vector<int> vA(A.size(), -1), vB(B.size(), -1);
			std::vector<bool> vAmbiguous(A.size()), vPositional(A.size());
			auto Pair = [&](int i, int j, bool Ambiguous) {
				vA[i] = j;
				vB[j] = i;
				vAmbiguous[i] = Ambiguous;
			};
			for(int Pass = 0; Pass < 3; ++Pass)
			{
				using TKey = std::tuple<int, std::string, uint64_t>;
				std::map<TKey, std::deque<int>> aBuckets[2];
				for(int Side = 0; Side < 2; ++Side)
				{
					const auto &Items = Side == 0 ? A : B;
					const auto &Matches = Side == 0 ? vA : vB;
					for(size_t i = 0; i < Items.size(); ++i)
					{
						Check.Tick();
						if(Matches[i] != -1)
							continue;
						const auto &Item = Items[i];
						const std::string &Name = Pass == 0 ? Item.m_Role : Item.m_Name;
						if(Pass != 1 && Name.empty())
							continue;
						aBuckets[Side][{Item.m_Compatible, Pass == 1 ? "" : Name, Pass == 1 ? Item.m_Hash : 0}].push_back(i);
					}
				}
				for(auto &[Key, Indices] : aBuckets[0])
				{
					Check.Tick();
					auto It = aBuckets[1].find(Key);
					if(It == aBuckets[1].end())
						continue;
					auto &Other = It->second;
					bool Ambiguous = Indices.size() != 1 || Other.size() != 1;
					if(Pass != 1 && Ambiguous)
						continue;
					if(Pass == 1 && Ambiguous)
					{
						// Exact duplicates can still have distinct names. Prefer those
						// names inside the content bucket, otherwise a table reorder
						// becomes a spurious pair of renames.
						std::map<std::string, std::vector<int>> aNamed[2];
						for(int i : Indices)
						{
							Check.Tick();
							if(!A[i].m_Name.empty())
								aNamed[0][A[i].m_Name].push_back(i);
						}
						for(int j : Other)
						{
							Check.Tick();
							if(!B[j].m_Name.empty())
								aNamed[1][B[j].m_Name].push_back(j);
						}
						for(const auto &[Name, Left] : aNamed[0])
						{
							Check.Tick();
							auto Right = aNamed[1].find(Name);
							if(Left.size() == 1 && Right != aNamed[1].end() && Right->second.size() == 1 && Exact(Left[0], Right->second[0]))
								Pair(Left[0], Right->second[0], false);
						}
						Indices.erase(std::remove_if(Indices.begin(), Indices.end(), [&](int i) { Check.Tick(); return vA[i] != -1; }), Indices.end());
						Other.erase(std::remove_if(Other.begin(), Other.end(), [&](int j) { Check.Tick(); return vB[j] != -1; }), Other.end());
					}
					while(!Indices.empty() && !Other.empty())
					{
						Check.Tick();
						int i = Indices.front(), j = Other.front();
						Indices.pop_front();
						Other.pop_front();
						if(Pass != 1 || Exact(i, j))
							Pair(i, j, Ambiguous);
					}
				}
			}
			// Monotone exact/name anchors delimit ordered spans. Crossed anchors are
			// already matched as moves, and are deliberately not used for guesses.
			int StartA = 0, StartB = 0;
			auto AlignSpan = [&](int EndA, int EndB) {
				std::vector<int> vLeft, vRight;
				for(int i = StartA; i < EndA; ++i)
				{
					Check.Tick();
					if(vA[i] == -1)
						vLeft.push_back(i);
				}
				for(int j = StartB; j < EndB; ++j)
				{
					Check.Tick();
					if(vB[j] == -1)
						vRight.push_back(j);
				}
				if(vLeft.size() != vRight.size())
					return;
				for(size_t k = 0; k < vLeft.size(); ++k)
				{
					Check.Tick();
					int i = vLeft[k], j = vRight[k];
					if(A[i].m_Compatible == B[j].m_Compatible)
					{
						Pair(i, j, true);
						vPositional[i] = true;
					}
				}
			};
			for(int i = 0; i < (int)A.size(); ++i)
			{
				Check.Tick();
				if(vA[i] >= StartB)
				{
					AlignSpan(i, vA[i]);
					StartA = i + 1;
					StartB = vA[i] + 1;
				}
			}
			AlignSpan(A.size(), B.size());
			std::vector<CPair> vPairs;
			std::vector<int> vSuffixMin(A.size() + 1, B.size());
			for(int i = (int)A.size() - 1; i >= 0; --i)
			{
				Check.Tick();
				vSuffixMin[i] = vA[i] < 0 ? vSuffixMin[i + 1] : std::min(vSuffixMin[i + 1], vA[i]);
			}
			int PrefixMax = -1;
			for(int i = 0; i < (int)A.size(); ++i)
			{
				Check.Tick();
				const int j = vA[i];
				bool Reordered = Ordered && j >= 0 && (j < PrefixMax || j > vSuffixMin[i + 1]);
				vPairs.push_back({i, j, vAmbiguous[i], Reordered, vPositional[i]});
				PrefixMax = std::max(PrefixMax, j);
			}
			for(int j = 0; j < (int)B.size(); ++j)
			{
				Check.Tick();
				if(vB[j] < 0)
					vPairs.push_back({-1, j, false, false});
			}
			return vPairs;
		}
		void AddName(CEntry &Entry, const std::string &Before, const std::string &After)
		{
			if(Before != After)
				Entry.m_vProperties.push_back({"Name", Before, After});
			Entry.m_Name = !After.empty() ? After : !Before.empty() ? Before :
										  "(unnamed)";
		}
		void CompareCells(CEntry &Entry, const CLayer &Before, const CLayer &After, CCheck &Check)
		{
			size_t a = 0, b = 0;
			while(a < Before.m_vCells.size() || b < After.m_vCells.size())
			{
				Check.Tick();
				const CCell *pA = a < Before.m_vCells.size() ? &Before.m_vCells[a] : nullptr;
				const CCell *pB = b < After.m_vCells.size() ? &After.m_vCells[b] : nullptr;
				if(pA && pB && pA->m_X == pB->m_X && pA->m_Y == pB->m_Y)
				{
					if(!(pA->m_Tile == pB->m_Tile))
						Entry.m_vTiles.push_back({pA->m_X, pA->m_Y, pA->m_Tile, pB->m_Tile});
					++a;
					++b;
				}
				else if(pA && (!pB || std::tie(pA->m_Y, pA->m_X) < std::tie(pB->m_Y, pB->m_X)))
				{
					Entry.m_vTiles.push_back({pA->m_X, pA->m_Y, pA->m_Tile, {}});
					++a;
				}
				else
				{
					Entry.m_vTiles.push_back({pB->m_X, pB->m_Y, {}, pB->m_Tile});
					++b;
				}
			}
		}
		void CompareObjects(CEntry &Entry, const CLayer &Before, const CLayer &After, CCheck &Check)
		{
			std::vector<CMatchItem> aItems[2];
			for(int Side = 0; Side < 2; ++Side)
			{
				for(const auto &Object : (Side == 0 ? Before : After).m_vObjects)
				{
					Check.Tick();
					CFingerprint Hash(Check);
					Hash.Add(Object.m_vProperties);
					aItems[Side].push_back({"", "", 0, Hash.m_Value});
				}
			}
			for(const auto &Pair : Match(aItems[0], aItems[1], [&](int a, int b) { return EqualProperties(Before.m_vObjects[a].m_vProperties, After.m_vObjects[b].m_vProperties, Check); }, Check))
				if(Pair.m_Before < 0 || Pair.m_After < 0 || Pair.m_Reordered || !EqualProperties(Before.m_vObjects[Pair.m_Before].m_vProperties, After.m_vObjects[Pair.m_After].m_vProperties, Check))
					Entry.m_vObjects.push_back(Pair);
		}
		void CompareResources(CResult &Result, const std::vector<CResource> &Before, const std::vector<CResource> &After, EEntryKind Kind, CCheck &Check)
		{
			std::vector<CMatchItem> aItems[2];
			for(int Side = 0; Side < 2; ++Side)
				for(const auto &Resource : Side == 0 ? Before : After)
				{
					Check.Tick();
					CFingerprint Hash(Check);
					Hash.Add(Resource.m_ContentKey.empty() ? ContentKey(Resource, Check) : Resource.m_ContentKey);
					aItems[Side].push_back({"", Resource.m_Name, 0, Hash.m_Value});
				}
			const auto vPairs = Match(aItems[0], aItems[1], [&](int a, int b) { return EqualProperties(Before[a].m_vProperties, After[b].m_vProperties, Check) && EqualBytes(Before[a].m_vData, After[b].m_vData, Check) && !Before[a].m_Missing && !After[b].m_Missing; }, Check, false);
			const CResource Empty;
			for(const auto &Pair : vPairs)
			{
				Check.Tick();
				const auto &A = Pair.m_Before >= 0 ? Before[Pair.m_Before] : Empty;
				const auto &B = Pair.m_After >= 0 ? After[Pair.m_After] : Empty;
				CEntry Entry;
				Entry.m_Kind = Kind;
				Entry.m_Before = Pair.m_Before;
				Entry.m_After = Pair.m_After;
				Entry.m_Ambiguous = Pair.m_Ambiguous;
				Entry.m_Positional = Pair.m_Positional;
				Entry.m_vProperties = Properties(A.m_vProperties, B.m_vProperties, Check);
				AddName(Entry, A.m_Name, B.m_Name);
				if(!EqualBytes(A.m_vData, B.m_vData, Check))
				{
					Entry.m_vProperties.push_back({"Content bytes", std::to_string(A.m_vData.size()), std::to_string(B.m_vData.size())});
					Entry.m_vProperties.push_back({"Content SHA-256", A.m_ContentKey.empty() ? ContentKey(A, Check) : A.m_ContentKey, B.m_ContentKey.empty() ? ContentKey(B, Check) : B.m_ContentKey});
				}
				if(A.m_Missing)
					Entry.m_vIssues.push_back("Before: missing resource content; equality cannot be established.");
				if(B.m_Missing)
					Entry.m_vIssues.push_back("After: missing resource content; equality cannot be established.");
				Result.m_vEntries.push_back(std::move(Entry));
			}
		}
	}

	bool CTile::operator==(const CTile &T) const
	{
		return m_Index == T.m_Index && m_Flags == T.m_Flags && m_Number == T.m_Number && m_Delay == T.m_Delay && m_Force == T.m_Force && m_MaxSpeed == T.m_MaxSpeed && m_Angle == T.m_Angle;
	}
	bool CEntry::Changed() const
	{
		return m_Before < 0 || m_After < 0 || m_Reordered || !m_vProperties.empty() || !m_vTiles.empty() || !m_vObjects.empty() || !m_vIssues.empty();
	}
	size_t CEntry::ChangeCount() const
	{
		return m_vProperties.size() + m_vTiles.size() + m_vObjects.size() + m_vIssues.size() + (m_Reordered ? 1 : 0) + (m_Before < 0 || m_After < 0 ? 1 : 0);
	}
	const char *CEntry::Status() const
	{
		return m_Before < 0 ? "+ Added" : m_After < 0 ? "- Deleted" :
					  Changed()           ? "-/+ Changed" :
								"= Unchanged";
	}
	size_t CResult::ChangeCount() const
	{
		size_t Count = 0;
		for(const auto &Entry : m_vEntries)
			Count += Entry.ChangeCount();
		return Count;
	}
	namespace
	{
		std::vector<CPropertyChange> Properties(const TProperties &Before, const TProperties &After, CCheck &Check)
		{
			std::vector<CPropertyChange> Result;
			std::map<std::string, const std::string *> aValues[2];
			for(const auto &A : Before)
			{
				Check.Tick();
				aValues[0].emplace(A.m_Name, &A.m_Value);
			}
			for(const auto &B : After)
			{
				Check.Tick();
				aValues[1].emplace(B.m_Name, &B.m_Value);
			}
			for(const auto &A : Before)
			{
				Check.Tick();
				auto It = aValues[1].find(A.m_Name);
				if(It == aValues[1].end() || !EqualBytes(A.m_Value, *It->second, Check))
					Result.push_back({A.m_Name, A.m_Value, It == aValues[1].end() ? "(absent)" : *It->second});
			}
			for(const auto &B : After)
			{
				Check.Tick();
				if(aValues[0].find(B.m_Name) == aValues[0].end())
					Result.push_back({B.m_Name, "(absent)", B.m_Value});
			}
			return Result;
		}
	}
	std::vector<CPropertyChange> CompareProperties(const TProperties &Before, const TProperties &After)
	{
		const FCancel Cancel;
		CCheck Check(Cancel);
		return Properties(Before, After, Check);
	}
	std::string ResourceContentKey(const CResource &Resource)
	{
		const FCancel Cancel;
		CCheck Check(Cancel);
		return ContentKey(Resource, Check);
	}
	CResult Compare(const CSnapshot &Before, const CSnapshot &After, const FCancel &Cancel)
	{
		CResult Result;
		CCheck Check(Cancel);
		try
		{
			Check.Tick();
			CEntry Info;
			Info.m_Name = "Map metadata";
			Info.m_Before = Info.m_After = 0;
			Info.m_vProperties = Properties(Before.m_vInfo, After.m_vInfo, Check);
			Result.m_vEntries.push_back(std::move(Info));
			CEntry Settings;
			Settings.m_Kind = EEntryKind::SETTINGS;
			Settings.m_Name = "Ordered server settings";
			Settings.m_Before = Settings.m_After = 0;
			AlignSettings(Settings, Before.m_vSettings, After.m_vSettings, Check);
			Result.m_vEntries.push_back(std::move(Settings));
			CompareResources(Result, Before.m_vImages, After.m_vImages, EEntryKind::IMAGE, Check);
			CompareResources(Result, Before.m_vSounds, After.m_vSounds, EEntryKind::SOUND, Check);
			CompareResources(Result, Before.m_vEnvelopes, After.m_vEnvelopes, EEntryKind::ENVELOPE, Check);
			std::vector<CMatchItem> aGroups[2];
			std::vector<std::vector<CMatchItem>> aLayers[2];
			for(int Side = 0; Side < 2; ++Side)
			{
				for(const auto &Group : (Side == 0 ? Before : After).m_vGroups)
				{
					Check.Tick();
					CFingerprint Hash(Check);
					Hash.Add(Group.m_vProperties);
					std::vector<CMatchItem> vLayers;
					for(const auto &Layer : Group.m_vLayers)
					{
						uint64_t Content = Fingerprint(Layer, Check);
						Hash.Add(Content);
						vLayers.push_back({Layer.m_Kind >= ELayerKind::GAME && Layer.m_Kind <= ELayerKind::TUNE ? LayerKindName(Layer.m_Kind) : "", Layer.m_Name, (int)Layer.m_Kind, Content});
					}
					aLayers[Side].push_back(std::move(vLayers));
					aGroups[Side].push_back({Group.m_Game ? "Game" : "", Group.m_Name, 0, Hash.m_Value});
				}
			}
			auto GroupEqual = [&](int a, int b) {
				const auto &A = Before.m_vGroups[a];
				const auto &B = After.m_vGroups[b];
				if(!EqualProperties(A.m_vProperties, B.m_vProperties, Check) || A.m_vLayers.size() != B.m_vLayers.size())
					return false;
				for(size_t i = 0; i < A.m_vLayers.size(); ++i)
					if(!Equal(A.m_vLayers[i], B.m_vLayers[i], Check))
						return false;
				return true;
			};
			const CGroup EmptyGroup;
			const CLayer EmptyLayer;
			const std::vector<CMatchItem> EmptyItems;
			for(const auto &GroupPair : Match(aGroups[0], aGroups[1], GroupEqual, Check))
			{
				Check.Tick();
				const auto &A = GroupPair.m_Before < 0 ? EmptyGroup : Before.m_vGroups[GroupPair.m_Before];
				const auto &B = GroupPair.m_After < 0 ? EmptyGroup : After.m_vGroups[GroupPair.m_After];
				CEntry GroupEntry;
				GroupEntry.m_Kind = EEntryKind::GROUP;
				GroupEntry.m_Before = GroupEntry.m_BeforeGroup = GroupPair.m_Before;
				GroupEntry.m_After = GroupEntry.m_AfterGroup = GroupPair.m_After;
				GroupEntry.m_Ambiguous = GroupPair.m_Ambiguous;
				GroupEntry.m_Positional = GroupPair.m_Positional;
				GroupEntry.m_Reordered = GroupPair.m_Reordered;
				GroupEntry.m_vProperties = Properties(A.m_vProperties, B.m_vProperties, Check);
				AddName(GroupEntry, A.m_Name, B.m_Name);
				const int Parent = Result.m_vEntries.size();
				Result.m_vEntries.push_back(std::move(GroupEntry));
				const auto &ItemsA = GroupPair.m_Before < 0 ? EmptyItems : aLayers[0][GroupPair.m_Before];
				const auto &ItemsB = GroupPair.m_After < 0 ? EmptyItems : aLayers[1][GroupPair.m_After];
				for(const auto &LayerPair : Match(ItemsA, ItemsB, [&](int a, int b) { return Equal(A.m_vLayers[a], B.m_vLayers[b], Check); }, Check))
				{
					const auto &LA = LayerPair.m_Before < 0 ? EmptyLayer : A.m_vLayers[LayerPair.m_Before];
					const auto &LB = LayerPair.m_After < 0 ? EmptyLayer : B.m_vLayers[LayerPair.m_After];
					CEntry Entry;
					Entry.m_Kind = EEntryKind::LAYER;
					Entry.m_Parent = Parent;
					Entry.m_BeforeGroup = GroupPair.m_Before;
					Entry.m_AfterGroup = GroupPair.m_After;
					Entry.m_Before = LayerPair.m_Before;
					Entry.m_After = LayerPair.m_After;
					Entry.m_Ambiguous = LayerPair.m_Ambiguous;
					Entry.m_Positional = LayerPair.m_Positional;
					Entry.m_Reordered = LayerPair.m_Reordered;
					Entry.m_vProperties = Properties(LA.m_vProperties, LB.m_vProperties, Check);
					AddName(Entry, LA.m_Name, LB.m_Name);
					if(LA.m_Width != LB.m_Width)
						Entry.m_vProperties.push_back({"Width (tiles)", std::to_string(LA.m_Width), std::to_string(LB.m_Width)});
					if(LA.m_Height != LB.m_Height)
						Entry.m_vProperties.push_back({"Height (tiles)", std::to_string(LA.m_Height), std::to_string(LB.m_Height)});
					for(const auto &Issue : LA.m_vIssues)
						Entry.m_vIssues.push_back("Before: " + Issue);
					for(const auto &Issue : LB.m_vIssues)
						Entry.m_vIssues.push_back("After: " + Issue);
					CompareCells(Entry, LA, LB, Check);
					CompareObjects(Entry, LA, LB, Check);
					Result.m_vEntries.push_back(std::move(Entry));
				}
			}
		}
		catch(const CCancelled &)
		{
			Result.m_vEntries.clear();
			Result.m_Cancelled = true;
		}
		return Result;
	}
	const char *LayerKindName(ELayerKind Kind)
	{
		static const char *s_apNames[] = {"Tiles", "Game", "Front", "Tele", "Speedup", "Switch", "Tune", "Quads", "Sounds", "Unknown"};
		return s_apNames[(int)Kind];
	}
	std::string TileDescription(const CTile &T, ELayerKind Kind)
	{
		std::ostringstream Text;
		Text << "index/type=" << (int)T.m_Index;
		if(Kind == ELayerKind::TILES || Kind == ELayerKind::GAME || Kind == ELayerKind::FRONT || Kind == ELayerKind::SWITCH)
			Text << ", flags=" << (int)T.m_Flags;
		if(Kind == ELayerKind::TELE || Kind == ELayerKind::SWITCH || Kind == ELayerKind::TUNE)
			Text << ", number=" << (int)T.m_Number;
		if(Kind == ELayerKind::SWITCH)
			Text << ", delay=" << (int)T.m_Delay;
		if(Kind == ELayerKind::SPEEDUP)
			Text << ", force=" << (int)T.m_Force << ", max speed=" << (int)T.m_MaxSpeed << ", angle=" << T.m_Angle;
		return Text.str();
	}
}
