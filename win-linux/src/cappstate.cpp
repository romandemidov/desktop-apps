#include "cappstate.h"
#include <mutex>
#include <QDebug>

class CAppState::CAppStatePriv
{
public:
    std::vector<AppState> m_states;
    std::condition_variable m_cv;
    std::mutex m_mutex;
};


CAppState::CAppState()
    : m_ptr(new CAppState::CAppStatePriv)
{
}

CAppState::~CAppState()
{
}

auto CAppState::contains(AppState s) -> bool
{
    return std::find(std::begin(m_ptr->m_states), std::end(m_ptr->m_states), s) != m_ptr->m_states.end();
}

auto CAppState::setState(AppState s) -> void
{
    if ( !contains(s) )
        m_ptr->m_states.push_back(s);
}

auto CAppState::clearState(AppState s) -> void
{
    auto it = std::find(std::begin(m_ptr->m_states), std::end(m_ptr->m_states), s);
    if ( it != std::end(m_ptr->m_states) )
        m_ptr->m_states.erase(it);

    m_ptr->m_cv.notify_all();
}

auto CAppState::await(AppState s, std::function<void()> callback) -> void
{
    if ( contains(s) ) {
        std::thread th_([=] {
            std::unique_lock<std::mutex> lock_(m_ptr->m_mutex);

            qDebug() << "locked";
            m_ptr->m_cv.wait(lock_, [=]{
                return !contains(s);
            });

            qDebug() << "unlocked. callback";
            callback();
        });
        th_.detach();
    } else {
        callback();
    }

}
