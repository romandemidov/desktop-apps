#ifndef CAPPSTATE_H
#define CAPPSTATE_H

#include <vector>
#include <functional>
#include <memory>

class CAppState
{
public:
    enum class AppState {
        Empty = 0,
        FullScreenWindow = 0x0001,
        ModalWidow = 0x0002
    };

    CAppState();
    ~CAppState();

    auto contains(AppState s) -> bool;
    auto setState(AppState s) -> void;
    auto clearState(AppState s) -> void;
    auto await(AppState s, std::function<void()>) -> void;

private:
    class CAppStatePriv;
    std::unique_ptr<CAppStatePriv> m_ptr;
};

#endif // CAPPSTATE_H
