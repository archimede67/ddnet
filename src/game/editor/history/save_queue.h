#ifndef GAME_EDITOR_HISTORY_SAVE_QUEUE_H
#define GAME_EDITOR_HISTORY_SAVE_QUEUE_H

#include <condition_variable>
#include <memory>
#include <mutex>
#include <utility>

namespace editor_history
{

	/** Orders non-abortable writer jobs without referring to a live document. */
	class CSaveQueue
	{
	public:
		class CTurn
		{
			friend class CSaveQueue;
			std::shared_ptr<CTurn> m_pPrevious;
			std::mutex m_Mutex;
			std::condition_variable m_Condition;
			bool m_Complete = false;

		public:
			/** Registration and submission must have the same order as the FIFO job pool. */
			void Wait()
			{
				auto pPrevious = std::move(m_pPrevious);
				if(pPrevious)
				{
					std::unique_lock Lock(pPrevious->m_Mutex);
					pPrevious->m_Condition.wait(Lock, [&] { return pPrevious->m_Complete; });
				}
			}

			void Complete()
			{
				{
					std::lock_guard Lock(m_Mutex);
					m_Complete = true;
				}
				m_Condition.notify_all();
			}
		};

		class CGuard
		{
			std::shared_ptr<CTurn> m_pTurn;

		public:
			explicit CGuard(std::shared_ptr<CTurn> pTurn) :
				m_pTurn(std::move(pTurn)) { m_pTurn->Wait(); }
			~CGuard() { m_pTurn->Complete(); }
			CGuard(const CGuard &) = delete;
			CGuard &operator=(const CGuard &) = delete;
		};

	private:
		std::weak_ptr<CTurn> m_pLast;

	public:
		/** Register and submit on the main thread in FIFO order, across all destinations. */
		std::shared_ptr<CTurn> Register()
		{
			auto pTurn = std::make_shared<CTurn>();
			pTurn->m_pPrevious = m_pLast.lock();
			m_pLast = pTurn;
			return pTurn;
		}
	};

} // namespace editor_history

#endif
