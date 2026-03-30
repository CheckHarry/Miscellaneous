#include <iostream>
#include <vector>
#include <utility>
#include <type_traits>

// ====================================================================
// A toy implementation of C++20-style ranges.
// Implements: filter, drop, stride views with pipe (|) syntax.
// ====================================================================

namespace toy {

// --------------------------------------------------------------------
// 1. CONCEPT: A range is anything with begin() and end().
// --------------------------------------------------------------------

template<class T>
concept range = requires(T& t) {
    t.begin();
    t.end();
};

template<range R> using iterator_t = decltype(std::declval<R&>().begin());
template<range R> using sentinel_t = decltype(std::declval<R&>().end());

// Tag to mark something as a view (real std::ranges uses enable_view)
struct view_base {};

// --------------------------------------------------------------------
// 2. ref_view: wraps a container by reference (like std::views::all).
//    Doesn't own the data — just holds a pointer to it.
// --------------------------------------------------------------------

template<range R>
struct ref_view : view_base {
    R* r_;
    ref_view(R& r) : r_(&r) {}
    auto begin() { return r_->begin(); }
    auto end()   { return r_->end(); }
};

// If it's already a view, pass through; otherwise wrap in ref_view.
template<class R>
auto as_view(R&& r) {
    if constexpr (std::is_base_of_v<view_base, std::remove_cvref_t<R>>)
        return std::forward<R>(r);
    else
        return ref_view<std::remove_reference_t<R>>(r);
}

// --------------------------------------------------------------------
// 3. PIPE MACHINERY: adaptor_closure enables  range | adaptor
//
//    views::filter(pred) returns an adaptor_closure storing a lambda.
//    When you write  range | adaptor, operator| calls that lambda
//    with the range, which constructs the view.
// --------------------------------------------------------------------

template<class Fn>
struct adaptor_closure {
    Fn fn_;
    adaptor_closure(Fn f) : fn_(std::move(f)) {}

    template<class R>
    friend auto operator|(R&& r, const adaptor_closure& a) {
        return a.fn_(std::forward<R>(r));
    }
};
template<class Fn> adaptor_closure(Fn) -> adaptor_closure<Fn>;

// ====================================================================
// 4. THE VIEWS — each stores the upstream view + parameters,
//    and defines a lazy iterator that does work on-the-fly.
// ====================================================================

// -------------------- drop_view --------------------
// Skips the first N elements.  No custom iterator needed:
// begin() just advances the underlying iterator by N.

template<range V>
struct drop_view : view_base {
    V   base_;
    int count_;

    drop_view(V base, int n) : base_(std::move(base)), count_(n) {}

    auto begin() {
        auto it = base_.begin();
        for (int i = 0; i < count_ && it != base_.end(); ++i)
            ++it;
        return it;
    }

    auto end() { return base_.end(); }
};

// -------------------- filter_view --------------------
// Lazily skips elements that don't satisfy a predicate.
// The iterator's operator++ does the skipping.

template<range V, class Pred>
struct filter_view : view_base {
    V    base_;
    Pred pred_;

    struct iterator {
        iterator_t<V> current_;
        sentinel_t<V> end_;
        Pred*         pred_;

        void find_next_valid() {
            while (current_ != end_ && !(*pred_)(*current_))
                ++current_;
        }

        decltype(auto) operator*()  const { return *current_; }
        iterator& operator++()            { ++current_; find_next_valid(); return *this; }
        bool operator==(const iterator& o) const { return current_ == o.current_; }
        bool operator!=(const iterator& o) const { return current_ != o.current_; }
    };

    filter_view(V base, Pred pred)
        : base_(std::move(base)), pred_(std::move(pred)) {}

    iterator begin() {
        iterator it{base_.begin(), base_.end(), &pred_};
        it.find_next_valid();    // advance to first valid element
        return it;
    }
    iterator end() {
        return {base_.end(), base_.end(), &pred_};
    }
};

// -------------------- stride_view --------------------
// Yields every Nth element.  The iterator's operator++
// advances the underlying iterator N times.

template<range V>
struct stride_view : view_base {
    V   base_;
    int stride_;

    struct iterator {
        iterator_t<V> current_;
        sentinel_t<V> end_;
        int           stride_;

        decltype(auto) operator*()  const { return *current_; }
        iterator& operator++() {
            for (int i = 0; i < stride_ && current_ != end_; ++i)
                ++current_;
            return *this;
        }
        bool operator==(const iterator& o) const { return current_ == o.current_; }
        bool operator!=(const iterator& o) const { return current_ != o.current_; }
    };

    stride_view(V base, int s) : base_(std::move(base)), stride_(s) {}

    iterator begin() { return {base_.begin(), base_.end(), stride_}; }
    iterator end()   { return {base_.end(),   base_.end(), stride_}; }
};

// --------------------------------------------------------------------
// 5. ADAPTOR FACTORIES: views::drop(n), views::filter(pred), etc.
//    Each returns an adaptor_closure wrapping a lambda that,
//    when given a range, constructs the corresponding view.
// --------------------------------------------------------------------

namespace views {
    inline auto drop(int n) {
        return adaptor_closure([=](auto&& r) {
            return drop_view(as_view(std::forward<decltype(r)>(r)), n);
        });
    }
    inline auto filter(auto pred) {
        return adaptor_closure([=](auto&& r) {
            return filter_view(as_view(std::forward<decltype(r)>(r)), pred);
        });
    }
    inline auto stride(int n) {
        return adaptor_closure([=](auto&& r) {
            return stride_view(as_view(std::forward<decltype(r)>(r)), n);
        });
    }
}

} // namespace toy

// ====================================================================
// DEMO
// ====================================================================

void example1() {
    std::vector<int> data = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20};

    auto result = data
        | toy::views::drop(2)
        | toy::views::filter([](int x){ return x % 2 != 0; })
        | toy::views::stride(2);

    std::cout << "data:  ";
    for (int x : data) std::cout << x << " ";

    std::cout << "\n\nPipeline: drop(2) | filter(odd) | stride(2)\n";
    std::cout << "  after drop(2):      ";
    for (auto x : data | toy::views::drop(2))
        std::cout << x << " ";

    std::cout << "\n  after filter(odd):  ";
    for (auto x : data | toy::views::drop(2)
                       | toy::views::filter([](int x){ return x%2!=0; }))
        std::cout << x << " ";

    std::cout << "\n  after stride(2):    ";
    for (auto x : result)
        std::cout << x << " ";

    std::cout << "\n";
}

void example2() {
    std::vector<int> data = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20};

    auto result = data
        | toy::views::filter([](int x){ return x % 2 != 0; })
        | toy::views::drop(2);

    for (auto x : result)
        std::cout << x << " ";

    std::cout << "\n";
}

int main() {
    example2();
}