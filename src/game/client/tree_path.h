#ifndef GAME_CLIENT_TREE_PATH_H
#define GAME_CLIENT_TREE_PATH_H

#include <vector>

// A path is a list of numbers.
// It is mainly used to identify a node (the path to a node) within the tree.
class CTreeNodePath
{
	using PathElements = std::vector<unsigned int>;

public:
	CTreeNodePath(const PathElements &Path) :
		m_vPathElements(Path) {}

	CTreeNodePath(const std::initializer_list<unsigned int> &Path) :
		m_vPathElements(Path) {}

	CTreeNodePath() :
		CTreeNodePath(PathElements{}) {}

	size_t Length() const { return m_vPathElements.size(); }
	bool Empty() const { return m_vPathElements.empty(); }

	CTreeNodePath Parent() const
	{
		auto vParent = m_vPathElements;
		vParent.pop_back();
		return vParent;
	}

	CTreeNodePath Child(const unsigned Index) const
	{
		auto vChild = m_vPathElements;
		vChild.push_back(Index);
		return vChild;
	}

	CTreeNodePath NextSibling() const
	{
		auto vSibling = m_vPathElements;
		vSibling.back()++;
		return vSibling;
	}

	CTreeNodePath PreviousSibling() const
	{
		auto vSibling = m_vPathElements;
		if(vSibling.back() > 0)
			vSibling.back()--;
		return vSibling;
	}

	unsigned int Index() const { return m_vPathElements.back(); }

	void Print() const
	{
		printf("/");
		for(const unsigned p : m_vPathElements)
			printf("%d/", p);
	}

	std::vector<unsigned int> &Elements() { return m_vPathElements; }

	unsigned int Pop()
	{
		const int Back = Elements().back();
		Elements().pop_back();
		return Back;
	}
	void Push(const unsigned Element = 0)
	{
		m_vPathElements.push_back(Element);
	}
	void Clear() { Elements().clear(); }

public: // Operators
	unsigned int operator[](const size_t Index) const { return m_vPathElements[Index]; }
	bool operator==(const CTreeNodePath &Other) const { return Other.m_vPathElements == m_vPathElements; }
	bool operator!=(const CTreeNodePath &Other) const { return !operator==(Other); }
	bool operator<(const CTreeNodePath &Other) const { return m_vPathElements < Other.m_vPathElements; }
	bool operator<=(const CTreeNodePath &Other) const { return m_vPathElements <= Other.m_vPathElements; }
	bool operator>(const CTreeNodePath &Other) const { return m_vPathElements > Other.m_vPathElements; }
	bool operator>=(const CTreeNodePath &Other) const { return m_vPathElements >= Other.m_vPathElements; }
	CTreeNodePath operator/(const unsigned int Rhs) const { return CTreeNodePath(m_vPathElements, Rhs); }
	CTreeNodePath operator/=(const unsigned int Rhs) const { return (*this) / Rhs; }
	CTreeNodePath operator--(int)
	{
		CTreeNodePath Current = *this;
		Elements().back()--;
		return Current;
	}

	CTreeNodePath operator++(int)
	{
		CTreeNodePath Current = *this;
		Elements().back()++;
		return Current;
	}

public: // Iterators
	PathElements::iterator begin() { return m_vPathElements.begin(); }
	PathElements::iterator end() { return m_vPathElements.end(); }
	PathElements::const_iterator begin() const { return m_vPathElements.begin(); }
	PathElements::const_iterator end() const { return m_vPathElements.end(); }

	template<int N>
	void ToString(char (&aBuf)[N])
	{
		str_copy(aBuf, "");
		for(const auto &Element : m_vPathElements)
		{
			str_format(aBuf, sizeof(aBuf), "%s/%d", aBuf, Element);
		}
	}

private:
	template<typename... T>
	CTreeNodePath(const PathElements &Path, const T... Children) :
		CTreeNodePath(Path)
	{
		(m_vPathElements.push_back(Children), ...);
	}

private:
	PathElements m_vPathElements;
};

#endif
