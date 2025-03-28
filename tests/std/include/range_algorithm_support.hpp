// Copyright (c) Microsoft Corporation.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <cassert>
#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>

#include <is_permissive.hpp>

namespace ranges = std::ranges;

template <class T>
constexpr T* nullptr_to = nullptr;

template <bool>
struct borrowed { // borrowed<true> is a borrowed_range; borrowed<false> is not
    int* begin() const;
    int* end() const;
};

template <>
inline constexpr bool ranges::enable_borrowed_range<borrowed<true>> = true;

struct boolish {
    bool value_ = true;

    constexpr operator bool() const noexcept {
        return value_;
    }

    [[nodiscard]] constexpr boolish operator!() const noexcept {
        return {!value_};
    }
};

template <class T, std::size_t N>
struct holder {
    static_assert(N < ~std::size_t{0} / sizeof(T));

    alignas(T) unsigned char space[N * sizeof(T)];

    auto as_span() {
        return std::span<T, N>{reinterpret_cast<T*>(space + 0), N};
    }
};

namespace test {
    using std::assignable_from, std::conditional_t, std::convertible_to, std::copy_constructible, std::derived_from,
        std::exchange, std::ptrdiff_t, std::span;

    using output     = std::output_iterator_tag;
    using input      = std::input_iterator_tag;
    using fwd        = std::forward_iterator_tag;
    using bidi       = std::bidirectional_iterator_tag;
    using random     = std::random_access_iterator_tag;
    using contiguous = std::contiguous_iterator_tag;

    template <class T>
    void operator&(T&&) {
        static_assert(false);
    }

    template <class T, class U>
    void operator,(T&&, U&&) {
        static_assert(false);
    }

    enum class CanDifference : bool { no, yes };
    enum class CanCompare : bool { no, yes };
    enum class ProxyRef { no, yes, prvalue, xvalue };
    enum class WrappedState {
        wrapped,
        unwrapped,
        ignorant,
    };

    template <class Derived, WrappedState Wrapped>
    struct prevent_inheriting_unwrap_base {
        using _Prevent_inheriting_unwrap = Derived;
    };
    template <class Derived>
    struct prevent_inheriting_unwrap_base<Derived, WrappedState::ignorant> {};

    [[nodiscard]] constexpr bool is_wrapped(WrappedState s) {
        return s == WrappedState::wrapped;
    }

    template <WrappedState W1, WrappedState W2>
    concept compatible_wrapped_state = (W1 == W2) || (W1 == WrappedState::wrapped && W2 == WrappedState::ignorant)
                                    || (W1 == WrappedState::ignorant && W2 == WrappedState::wrapped);

    template <class T>
    [[nodiscard]] constexpr bool to_bool(T const t) noexcept {
        static_assert(std::is_enum_v<T> && std::same_as<std::underlying_type_t<T>, bool>);
        return static_cast<bool>(t);
    }

    template <class Element, WrappedState Wrapped = WrappedState::wrapped>
    class sentinel : public prevent_inheriting_unwrap_base<sentinel<Element, Wrapped>, Wrapped> {
        Element* ptr_ = nullptr;

    public:
        sentinel() = default;
        constexpr explicit sentinel(Element* ptr) noexcept : ptr_{ptr} {}

        [[nodiscard]] constexpr Element* peek() const noexcept {
            return ptr_;
        }

        using unwrap    = sentinel<Element, WrappedState::unwrapped>;
        using Constinel = sentinel<const Element, Wrapped>;

        constexpr operator Constinel() && noexcept {
            return Constinel{exchange(ptr_, nullptr)};
        }

        constexpr operator Constinel() const& noexcept {
            return Constinel{ptr_};
        }

        [[nodiscard]] constexpr auto _Unwrapped() const noexcept
            requires (is_wrapped(Wrapped))
        {
            return unwrap{ptr_};
        }

        static constexpr bool _Unwrap_when_unverified = true;

        constexpr void _Seek_to(unwrap const& s) noexcept
            requires (is_wrapped(Wrapped))
        {
            ptr_ = s.peek();
        }

        [[nodiscard]] friend constexpr boolish operator==(sentinel const s, Element* const ptr) noexcept {
            return {s.ptr_ == ptr};
        }
        [[nodiscard]] friend constexpr boolish operator==(Element* const ptr, sentinel const s) noexcept {
            return {s.ptr_ == ptr};
        }
        [[nodiscard]] friend constexpr boolish operator!=(sentinel const s, Element* const ptr) noexcept {
            return !(s == ptr);
        }
        [[nodiscard]] friend constexpr boolish operator!=(Element* const ptr, sentinel const s) noexcept {
            return !(s == ptr);
        }

        [[nodiscard]] friend constexpr ptrdiff_t operator-(sentinel const s, Element* const ptr) noexcept {
            return s.ptr_ - ptr;
        }
        [[nodiscard]] friend constexpr ptrdiff_t operator-(Element* const ptr, sentinel const s) noexcept {
            return ptr - s.ptr_;
        }
    };

    template <class T, class U>
    concept CanEq = requires(T const& t, U const& u) {
        { t == u } -> convertible_to<bool>;
    };

    template <class T, class U>
    concept CanNEq = requires(T const& t, U const& u) {
        { t != u } -> convertible_to<bool>;
    };

    template <class T, class U>
    concept CanLt = requires(T const& t, U const& u) {
        { t < u } -> convertible_to<bool>;
    };

    template <class T, class U>
    concept CanLtE = requires(T const& t, U const& u) {
        { t <= u } -> convertible_to<bool>;
    };

    template <class T, class U>
    concept CanGt = requires(T const& t, U const& u) {
        { t > u } -> convertible_to<bool>;
    };

    template <class T, class U>
    concept CanGtE = requires(T const& t, U const& u) {
        { t >= u } -> convertible_to<bool>;
    };

    template <class Category, class Element>
    class proxy_reference {
        Element& ref_;

        using Value = std::remove_cv_t<Element>;

    public:
        constexpr explicit proxy_reference(Element& r) : ref_{r} {}
        proxy_reference(proxy_reference const&) = default;

        constexpr proxy_reference const& operator=(proxy_reference const& that) const
            requires assignable_from<Element&, Element&>
        {
            ref_ = that.ref_;
            return *this;
        }

        constexpr operator Element&() const
            requires derived_from<Category, input>
        {
            return ref_;
        }

        template <class T>
            requires (!std::same_as<std::remove_cvref_t<T>, proxy_reference> && assignable_from<Element&, T>)
        constexpr void operator=(T&& val) const {
            ref_ = std::forward<T>(val);
        }

        template <class Cat, class Elem>
        [[nodiscard]] constexpr boolish operator==(proxy_reference<Cat, Elem> that) const
            requires CanEq<Element, Elem>
        {
            return {ref_ == that.peek()};
        }
        template <class Cat, class Elem>
        [[nodiscard]] constexpr boolish operator!=(proxy_reference<Cat, Elem> that) const
            requires CanNEq<Element, Elem>
        {
            return {ref_ != that.peek()};
        }
        template <class Cat, class Elem>
        [[nodiscard]] constexpr boolish operator<(proxy_reference<Cat, Elem> that) const
            requires CanLt<Element, Elem>
        {
            return {ref_ < that.peek()};
        }
        template <class Cat, class Elem>
        [[nodiscard]] constexpr boolish operator>(proxy_reference<Cat, Elem> that) const
            requires CanGt<Element, Elem>
        {
            return {ref_ > that.peek()};
        }
        template <class Cat, class Elem>
        [[nodiscard]] constexpr boolish operator<=(proxy_reference<Cat, Elem> that) const
            requires CanLtE<Element, Elem>
        {
            return {ref_ <= that.peek()};
        }
        template <class Cat, class Elem>
        [[nodiscard]] constexpr boolish operator>=(proxy_reference<Cat, Elem> that) const
            requires CanGtE<Element, Elem>
        {
            return {ref_ >= that.peek()};
        }

        [[nodiscard]] friend constexpr boolish operator==(proxy_reference r, Value const& val)
            requires CanEq<Element, Value>
        {
            return {r.ref_ == val};
        }
        [[nodiscard]] friend constexpr boolish operator==(Value const& val, proxy_reference r)
            requires CanEq<Element, Value>
        {
            return {r.ref_ == val};
        }
        [[nodiscard]] friend constexpr boolish operator!=(proxy_reference r, Value const& val)
            requires CanNEq<Element, Value>
        {
            return {r.ref_ != val};
        }
        [[nodiscard]] friend constexpr boolish operator!=(Value const& val, proxy_reference r)
            requires CanNEq<Element, Value>
        {
            return {r.ref_ != val};
        }
        [[nodiscard]] friend constexpr boolish operator<(Value const& val, proxy_reference r)
            requires CanLt<Value, Element>
        {
            return {val < r.ref_};
        }
        [[nodiscard]] friend constexpr boolish operator<(proxy_reference r, Value const& val)
            requires CanLt<Element, Value>
        {
            return {r.ref_ < val};
        }
        [[nodiscard]] friend constexpr boolish operator>(Value const& val, proxy_reference r)
            requires CanGt<Value, Element>
        {
            return {val > r.ref_};
        }
        [[nodiscard]] friend constexpr boolish operator>(proxy_reference r, Value const& val)
            requires CanGt<Element, Value>
        {
            return {r.ref_ > val};
        }
        [[nodiscard]] friend constexpr boolish operator<=(Value const& val, proxy_reference r)
            requires CanLtE<Value, Element>
        {
            return {val <= r.ref_};
        }
        [[nodiscard]] friend constexpr boolish operator<=(proxy_reference r, Value const& val)
            requires CanLtE<Element, Value>
        {
            return {r.ref_ <= val};
        }
        [[nodiscard]] friend constexpr boolish operator>=(Value const& val, proxy_reference r)
            requires CanGtE<Value, Element>
        {
            return {val >= r.ref_};
        }
        [[nodiscard]] friend constexpr boolish operator>=(proxy_reference r, Value const& val)
            requires CanGtE<Element, Value>
        {
            return {r.ref_ >= val};
        }

        [[nodiscard]] constexpr Element& peek() const noexcept {
            return ref_;
        }
    };

    template <class Ref>
    struct common_reference {
        Ref ref_;

        common_reference(Ref r) : ref_{static_cast<Ref>(r)} {}

        template <class Cat, class Elem>
            requires convertible_to<Elem&, Ref>
        common_reference(proxy_reference<Cat, Elem> pref) : ref_{pref.peek()} {}
    };
} // namespace test

template <class Cat, class Elem, class U, template <class> class TQuals, template <class> class UQuals>
    requires std::common_reference_with<Elem&, UQuals<U>>
struct std::basic_common_reference<::test::proxy_reference<Cat, Elem>, U, TQuals, UQuals> {
    using type = common_reference_t<Elem&, UQuals<U>>;
};

template <class T, class Cat, class Elem, template <class> class TQuals, template <class> class UQuals>
    requires std::common_reference_with<TQuals<T>, Elem&>
struct std::basic_common_reference<T, ::test::proxy_reference<Cat, Elem>, TQuals, UQuals> {
    using type = common_reference_t<TQuals<T>, Elem&>;
};

template <class Cat1, class Elem1, class Cat2, class Elem2, template <class> class TQuals,
    template <class> class UQuals>
    requires std::common_reference_with<Elem1&, Elem2&>
struct std::basic_common_reference<::test::proxy_reference<Cat1, Elem1>, ::test::proxy_reference<Cat2, Elem2>, TQuals,
    UQuals> {
    using type = common_reference_t<Elem1&, Elem2&>;
};

namespace test {
    template <class T>
    struct init_list_not_constructible_sentinel {
        init_list_not_constructible_sentinel() = default;
        init_list_not_constructible_sentinel(T*) {}

        template <class U>
        init_list_not_constructible_sentinel(std::initializer_list<U>) = delete;
    };

    template <class T>
    struct init_list_not_constructible_iterator {
        using value_type      = T;
        using difference_type = int;

        init_list_not_constructible_iterator() = default;
        init_list_not_constructible_iterator(T*) {}

        template <class U>
        init_list_not_constructible_iterator(std::initializer_list<U>) = delete;

        T& operator*() const; // not defined
        init_list_not_constructible_iterator& operator++(); // not defined
        init_list_not_constructible_iterator operator++(int); // not defined

        bool operator==(init_list_not_constructible_iterator) const; // not defined
        bool operator==(init_list_not_constructible_sentinel<T>) const; // not defined
    };

    static_assert(std::forward_iterator<init_list_not_constructible_iterator<int>>);
    static_assert(
        std::sentinel_for<init_list_not_constructible_sentinel<int>, init_list_not_constructible_iterator<int>>);

    template <class Category, class Element,
        // Model sized_sentinel_for along with sentinel?
        CanDifference Diff = CanDifference{derived_from<Category, random>},
        // Model sentinel_for with self (and sized_sentinel_for if Diff implies copyable)?
        CanCompare Eq = CanCompare{derived_from<Category, fwd>},
        // Use a ProxyRef reference type (instead of Element&)?
        ProxyRef Proxy = ProxyRef{!derived_from<Category, contiguous>},
        // Interact with the STL's iterator unwrapping machinery?
        WrappedState Wrapped = WrappedState::wrapped>
        requires (to_bool(Eq) || !derived_from<Category, fwd>)
              && (Proxy == ProxyRef::no || !derived_from<Category, contiguous>)
    class iterator
        : public prevent_inheriting_unwrap_base<iterator<Category, Element, Diff, Eq, Proxy, Wrapped>, Wrapped> {
        Element* ptr_;

        template <class T>
        static constexpr bool at_least = derived_from<Category, T>;

        using ReferenceType = conditional_t<Proxy == ProxyRef::yes, proxy_reference<Category, Element>,
            conditional_t<Proxy == ProxyRef::prvalue, std::remove_cv_t<Element>,
                conditional_t<Proxy == ProxyRef::xvalue, Element&&, Element&>>>;

        struct post_increment_proxy {
            Element* ptr_;

            const post_increment_proxy& operator*() const noexcept {
                return *this;
            }

            template <class T>
                requires std::indirectly_writable<Element*, T>
            const post_increment_proxy& operator=(T&& t) const noexcept {
                *ptr_ = std::forward<T>(t);
                return *this;
            }
        };

    public:
        using Consterator = iterator<Category, const Element, Diff, Eq, Proxy, Wrapped>;

        // output iterator operations
        iterator()
            requires at_least<fwd> || (Eq == CanCompare::yes)
        = default;

        constexpr explicit iterator(Element* ptr) noexcept : ptr_{ptr} {}

        constexpr iterator(iterator&& that) noexcept : ptr_{exchange(that.ptr_, nullptr)} {}
        constexpr iterator& operator=(iterator&& that) noexcept {
            ptr_ = exchange(that.ptr_, nullptr);
            return *this;
        }

        constexpr operator Consterator() && noexcept {
            return Consterator{exchange(ptr_, nullptr)};
        }

        [[nodiscard]] constexpr Element* peek() const noexcept {
            return ptr_;
        }

        [[nodiscard]] constexpr ReferenceType operator*() const noexcept {
            return static_cast<ReferenceType>(*ptr_);
        }

        template <WrappedState OtherWrapped>
        [[nodiscard]] friend constexpr boolish operator==(
            iterator const& i, sentinel<Element, OtherWrapped> const& s) noexcept
            requires compatible_wrapped_state<Wrapped, OtherWrapped>
        {
            return boolish{i.peek() == s.peek()};
        }
        template <WrappedState OtherWrapped>
        [[nodiscard]] friend constexpr boolish operator==(
            sentinel<Element, OtherWrapped> const& s, iterator const& i) noexcept
            requires compatible_wrapped_state<Wrapped, OtherWrapped>
        {
            return i == s;
        }

        template <WrappedState OtherWrapped>
        [[nodiscard]] friend constexpr boolish operator!=(
            iterator const& i, sentinel<Element, OtherWrapped> const& s) noexcept
            requires compatible_wrapped_state<Wrapped, OtherWrapped>
        {
            return !(i == s);
        }
        template <WrappedState OtherWrapped>
        [[nodiscard]] friend constexpr boolish operator!=(
            sentinel<Element, OtherWrapped> const& s, iterator const& i) noexcept
            requires compatible_wrapped_state<Wrapped, OtherWrapped>
        {
            return !(i == s);
        }

        constexpr iterator& operator++() & noexcept {
            ++ptr_;
            return *this;
        }

        constexpr post_increment_proxy operator++(int) & noexcept
            requires std::is_same_v<Category, output>
        {
            post_increment_proxy result{ptr_};
            ++ptr_;
            return result;
        }

        auto operator--() & {
            static_assert(false);
        }
        auto operator--(int) & {
            static_assert(false);
        }

        friend void iter_swap(iterator const&, iterator const&)
            requires std::is_same_v<Category, output>
        {
            static_assert(false);
        }

        void operator<(iterator const&) const {
            static_assert(false);
        }
        void operator>(iterator const&) const {
            static_assert(false);
        }
        void operator<=(iterator const&) const {
            static_assert(false);
        }
        void operator>=(iterator const&) const {
            static_assert(false);
        }

        // input iterator operations:
        constexpr void operator++(int) & noexcept
            requires std::is_same_v<Category, input>
        {
            ++ptr_;
        }

        [[nodiscard]] friend constexpr Element&& iter_move(iterator const& i)
            requires at_least<input>
        {
            return std::move(*i.ptr_);
        }

        friend constexpr void iter_swap(iterator const& x, iterator const& y)
            noexcept(std::is_nothrow_swappable_v<Element>)
            requires at_least<input> && std::swappable<Element>
        {
            ranges::swap(*x.ptr_, *y.ptr_);
        }

        // forward iterator operations:
        constexpr iterator operator++(int) & noexcept
            requires at_least<fwd>
        {
            auto tmp = *this;
            ++ptr_;
            return tmp;
        }

        // sentinel operations (implied by forward iterator):
        iterator(iterator const&)
            requires (to_bool(Eq))
        = default;
        iterator& operator=(iterator const&)
            requires (to_bool(Eq))
        = default;

        constexpr operator Consterator() const& noexcept
            requires (to_bool(Eq))
        {
            return Consterator{ptr_};
        }

        [[nodiscard]] constexpr boolish operator==(iterator const& that) const noexcept
            requires (to_bool(Eq))
        {
            return {ptr_ == that.ptr_};
        }
        [[nodiscard]] constexpr boolish operator!=(iterator const& that) const noexcept
            requires (to_bool(Eq))
        {
            return !(*this == that);
        }

        // bidi iterator operations:
        constexpr iterator& operator--() & noexcept
            requires at_least<bidi>
        {
            --ptr_;
            return *this;
        }
        constexpr iterator operator--(int) & noexcept
            requires at_least<bidi>
        {
            auto tmp = *this;
            --ptr_;
            return tmp;
        }

        // random-access iterator operations:
        [[nodiscard]] constexpr boolish operator<(iterator const& that) const noexcept
            requires at_least<random>
        {
            return {ptr_ < that.ptr_};
        }
        [[nodiscard]] constexpr boolish operator>(iterator const& that) const noexcept
            requires at_least<random>
        {
            return that < *this;
        }
        [[nodiscard]] constexpr boolish operator<=(iterator const& that) const noexcept
            requires at_least<random>
        {
            return !(that < *this);
        }
        [[nodiscard]] constexpr boolish operator>=(iterator const& that) const noexcept
            requires at_least<random>
        {
            return !(*this < that);
        }
        [[nodiscard]] constexpr auto operator<=>(iterator const& that) const noexcept
            requires at_least<random>
        {
            return ptr_ <=> that.ptr_;
        }

        [[nodiscard]] constexpr ReferenceType operator[](ptrdiff_t const n) const& noexcept
            requires at_least<random>
        {
            return ReferenceType{ptr_[n]};
        }

        constexpr iterator& operator+=(ptrdiff_t const n) & noexcept
            requires at_least<random>
        {
            ptr_ += n;
            return *this;
        }
        constexpr iterator& operator-=(ptrdiff_t const n) & noexcept
            requires at_least<random>
        {
            ptr_ -= n;
            return *this;
        }

        [[nodiscard]] constexpr iterator operator+(ptrdiff_t const n) const noexcept
            requires at_least<random>
        {
            return iterator{ptr_ + n};
        }
        [[nodiscard]] friend constexpr iterator operator+(ptrdiff_t const n, iterator const& i) noexcept
            requires at_least<random>
        {
            return i + n;
        }

        [[nodiscard]] constexpr iterator operator-(ptrdiff_t const n) const noexcept
            requires at_least<random>
        {
            return iterator{ptr_ - n};
        }

        // contiguous iterator operations:
        [[nodiscard]] constexpr Element* operator->() const noexcept
            requires at_least<contiguous>
        {
            return ptr_;
        }

        // sized_sentinel_for operations:
        [[nodiscard]] constexpr ptrdiff_t operator-(iterator const& that) const noexcept
            requires at_least<random> || (to_bool(Diff) && to_bool(Eq))
        {
            return ptr_ - that.ptr_;
        }

        template <WrappedState OtherWrapped>
        [[nodiscard]] constexpr ptrdiff_t operator-(sentinel<Element, OtherWrapped> const& s) const noexcept
            requires compatible_wrapped_state<Wrapped, OtherWrapped> && (to_bool(Diff))
        {
            return ptr_ - s.peek();
        }
        template <WrappedState OtherWrapped>
        [[nodiscard]] friend constexpr ptrdiff_t operator-(
            sentinel<Element, OtherWrapped> const& s, iterator const& i) noexcept
            requires compatible_wrapped_state<Wrapped, OtherWrapped> && (to_bool(Diff))
        {
            return -(i - s);
        }

        using unwrap              = std::conditional_t<derived_from<Category, contiguous>, Element*,
                         iterator<Category, Element, Diff, Eq, Proxy, WrappedState::unwrapped>>;
        using unwrapping_ignorant = iterator<Category, Element, Diff, Eq, Proxy, WrappedState::ignorant>;

        [[nodiscard]] constexpr auto _Unwrapped() const& noexcept
            requires (is_wrapped(Wrapped) && to_bool(Eq))
        {
            return unwrap{ptr_};
        }

        [[nodiscard]] constexpr auto _Unwrapped() && noexcept
            requires (is_wrapped(Wrapped))
        {
            return unwrap{exchange(ptr_, nullptr)};
        }

        static constexpr bool _Unwrap_when_unverified = true;

        constexpr void _Seek_to(unwrap const& i) noexcept
            requires (is_wrapped(Wrapped) && to_bool(Eq))
        {
            if constexpr (at_least<contiguous>) {
                ptr_ = i;
            } else {
                ptr_ = i.peek();
            }
        }

        constexpr void _Seek_to(unwrap&& i) noexcept
            requires (is_wrapped(Wrapped))
        {
            if constexpr (at_least<contiguous>) {
                ptr_ = i;
            } else {
                ptr_ = i.peek();
            }
        }
    };

    template <class Category, bool IsForward, bool IsProxy, bool EqAndCopy>
    struct iterator_traits_base {};

    template <class Category>
    struct iterator_traits_base<Category, true, false, true> {
        using iterator_category = Category;
    };

    template <class Category>
    struct iterator_traits_base<Category, true, true, true> {
        using iterator_category = input;
    };

    template <class Category, bool IsProxy>
    struct iterator_traits_base<Category, false, IsProxy, true> {
        using iterator_category = input;
    };
} // namespace test

template <class Category, class Element, ::test::CanDifference Diff, ::test::CanCompare Eq, ::test::ProxyRef Proxy,
    ::test::WrappedState Wrapped>
struct std::iterator_traits<::test::iterator<Category, Element, Diff, Eq, Proxy, Wrapped>>
    : ::test::iterator_traits_base<Category, derived_from<Category, forward_iterator_tag>,
          Proxy == ::test::ProxyRef::yes, Eq == ::test::CanCompare::yes> {
    using iterator_concept = Category;
    using value_type       = remove_cv_t<Element>;
    using difference_type  = ptrdiff_t;
    using pointer          = conditional_t<derived_from<Category, contiguous_iterator_tag>, Element*, void>;
    using reference        = iter_reference_t<::test::iterator<Category, Element, Diff, Eq, Proxy, Wrapped>>;
};

template <class Element, ::test::CanDifference Diff, ::test::WrappedState Wrapped>
struct std::pointer_traits<::test::iterator<std::contiguous_iterator_tag, Element, Diff, ::test::CanCompare::yes,
    ::test::ProxyRef::no, Wrapped>> {
    using pointer         = ::test::iterator<contiguous_iterator_tag, Element, Diff, ::test::CanCompare::yes,
                ::test::ProxyRef::no, Wrapped>;
    using element_type    = Element;
    using difference_type = ptrdiff_t;

    [[nodiscard]] static constexpr element_type* to_address(pointer const& x) noexcept {
        return x.peek();
    }
};

namespace test {
    enum class Sized : bool { no, yes };
    enum class Common : bool { no, yes };
    enum class CanView : bool { no, yes };
    enum class Copyability { immobile, move_only, copyable };

    namespace detail {
        template <class Element, Copyability Copy>
        class range_base {
        public:
            static_assert(Copy == Copyability::immobile);

            range_base() = delete;
            constexpr explicit range_base(span<Element> elements) noexcept : elements_{elements} {}

            range_base(const range_base&)            = delete;
            range_base& operator=(const range_base&) = delete;

        protected:
            [[nodiscard]] constexpr bool moved_from() const noexcept {
                return false;
            }
            span<Element> elements_;
        };

        template <class Element>
        class range_base<Element, Copyability::move_only> {
        public:
            range_base() = delete;
            constexpr explicit range_base(span<Element> elements) noexcept : elements_{elements} {}

            constexpr range_base(range_base&& that) noexcept
                : elements_{that.elements_}, moved_from_{that.moved_from_} {
                that.elements_   = {};
                that.moved_from_ = true;
            }

            constexpr range_base& operator=(range_base&& that) noexcept {
                elements_        = that.elements_;
                moved_from_      = that.moved_from_;
                that.elements_   = {};
                that.moved_from_ = true;
                return *this;
            }

        protected:
            [[nodiscard]] constexpr bool moved_from() const noexcept {
                return moved_from_;
            }

            span<Element> elements_;

        private:
            bool moved_from_ = false;
        };

        template <class Element>
        class range_base<Element, Copyability::copyable> {
        public:
            constexpr range_base() = default;
            constexpr explicit range_base(span<Element> elements) noexcept : elements_{elements} {}

            constexpr range_base(const range_base&)            = default;
            constexpr range_base& operator=(const range_base&) = default;

            constexpr range_base(range_base&& that) noexcept
                : elements_{that.elements_}, moved_from_{that.moved_from_} {
                that.elements_   = {};
                that.moved_from_ = true;
            }

            constexpr range_base& operator=(range_base&& that) noexcept {
                elements_        = that.elements_;
                moved_from_      = that.moved_from_;
                that.elements_   = {};
                that.moved_from_ = true;
                return *this;
            }

        protected:
            [[nodiscard]] constexpr bool moved_from() const noexcept {
                return moved_from_;
            }

            span<Element> elements_;

        private:
            bool moved_from_ = false;
        };
    } // namespace detail

    template <class Category, class Element,
        // Implement member size? (NB: Not equivalent to "Is this a sized_range?")
        Sized IsSized = Sized::no,
        // iterator and sentinel model sized_sentinel_for (also iterator and iterator if Eq)
        CanDifference Diff = CanDifference{derived_from<Category, random>},
        // Model common_range?
        Common IsCommon = Common::no,
        // Iterator models sentinel_for with self
        CanCompare Eq = CanCompare{derived_from<Category, fwd>},
        // Use a ProxyRef reference type?
        ProxyRef Proxy = ProxyRef{!derived_from<Category, contiguous>},
        // Should this range satisfy the view concept?
        CanView IsView = CanView::no,
        // Should this range type be copyable/movable/neither?
        Copyability Copy = IsView == CanView::yes ? Copyability::move_only : Copyability::immobile>
        requires (!to_bool(IsCommon) || to_bool(Eq)) && (to_bool(Eq) || !derived_from<Category, fwd>)
              && (Proxy == ProxyRef::no || !derived_from<Category, contiguous>)
              && (!to_bool(IsView) || Copy != Copyability::immobile)
    class range : public detail::range_base<Element, Copy> {
    private:
        mutable bool begin_called_ = false;
        using detail::range_base<Element, Copy>::elements_;

        using detail::range_base<Element, Copy>::moved_from;

    public:
        using I = iterator<Category, Element, Diff, Eq, Proxy, WrappedState::wrapped>;
        using S = conditional_t<to_bool(IsCommon), I, sentinel<Element, WrappedState::wrapped>>;
        using RebindAsMoveOnly =
            range<Category, Element, IsSized, Diff, IsCommon, Eq, Proxy, IsView, Copyability::move_only>;

        template <class OtherElement>
        using RebindElement = range<Category, OtherElement, IsSized, Diff, IsCommon, Eq, Proxy, IsView, Copy>;

        static constexpr ProxyRef proxy_ref = Proxy;

        using detail::range_base<Element, Copy>::range_base;

        [[nodiscard]] constexpr I begin() const noexcept {
            assert(!moved_from());
            if constexpr (!derived_from<Category, fwd>) {
                assert(!exchange(begin_called_, true));
            }
            return I{elements_.data()};
        }

        [[nodiscard]] constexpr S end() const noexcept {
            assert(!moved_from());
            return S{elements_.data() + elements_.size()};
        }

        [[nodiscard]] constexpr ptrdiff_t size() const noexcept
            requires (to_bool(IsSized))
        {
            assert(!moved_from());
            if constexpr (!derived_from<Category, fwd>) {
                assert(!begin_called_);
            }
            return static_cast<ptrdiff_t>(elements_.size());
        }

        [[nodiscard]] constexpr Element* data() const noexcept
            requires derived_from<Category, contiguous>
        {
            assert(!moved_from());
            return elements_.data();
        }

        using UI = I::unwrap;
        using US = conditional_t<to_bool(IsCommon), UI, sentinel<Element, WrappedState::unwrapped>>;

        [[nodiscard]] constexpr UI _Unchecked_begin() const noexcept {
            assert(!moved_from());
            if constexpr (!derived_from<Category, fwd>) {
                assert(!exchange(begin_called_, true));
            }
            return UI{elements_.data()};
        }
        [[nodiscard]] constexpr US _Unchecked_end() const noexcept {
            assert(!moved_from());
            return US{elements_.data() + elements_.size()};
        }

        void operator&() const {
            static_assert(false);
        }
        template <class T>
        friend void operator,(range const&, T&&) {
            static_assert(false);
        }
    };

    template <std::_Signed_integer_like I>
    [[nodiscard]] constexpr auto to_unsigned(I n) noexcept {
        if constexpr (std::signed_integral<I>) {
            return static_cast<std::make_unsigned_t<I>>(n);
        } else {
            return static_cast<std::_Unsigned128>(n);
        }
    }

    template <std::_Signed_integer_like Diff, std::input_iterator It>
    struct redifference_iterator_category_base {};

    template <std::_Signed_integer_like Diff, std::input_iterator It>
        requires std::signed_integral<Diff> && requires { typename std::iterator_traits<It>::iterator_category; }
    struct redifference_iterator_category_base<Diff, It> {
        using iterator_category = std::iterator_traits<It>::iterator_category;
    };

    template <std::_Signed_integer_like Diff, std::input_iterator It>
    class redifference_iterator : public redifference_iterator_category_base<Diff, It> {
    public:
        using iterator_concept = decltype([] {
            if constexpr (std::contiguous_iterator<It>) {
                return std::contiguous_iterator_tag{};
            } else if constexpr (std::random_access_iterator<It>) {
                return std::random_access_iterator_tag{};
            } else if constexpr (std::bidirectional_iterator<It>) {
                return std::bidirectional_iterator_tag{};
            } else if constexpr (std::forward_iterator<It>) {
                return std::forward_iterator_tag{};
            } else {
                return std::input_iterator_tag{};
            }
        }());
        using value_type       = std::iter_value_t<It>;
        using difference_type  = Diff;

        redifference_iterator() = default;
        constexpr explicit redifference_iterator(It it) : i_{std::move(it)} {}

        [[nodiscard]] constexpr decltype(auto) operator*() const {
            return *i_;
        }

        constexpr decltype(auto) operator->() const
            requires std::contiguous_iterator<It> || (requires(const It& i) { i.operator->(); })
        {
            if constexpr (std::contiguous_iterator<It>) {
                return std::to_address(i_);
            } else {
                return i_.operator->();
            }
        }

        constexpr redifference_iterator& operator++() {
            ++i_;
            return *this;
        }

        constexpr decltype(auto) operator++(int) {
            if constexpr (std::is_same_v<decltype(i_++), It>) {
                return redifference_iterator{i_++};
            } else {
                return i_++;
            }
        }

        constexpr redifference_iterator& operator--()
            requires std::bidirectional_iterator<It>
        {
            --i_;
            return *this;
        }

        constexpr redifference_iterator operator--(int)
            requires std::bidirectional_iterator<It>
        {
            return redifference_iterator{--i_};
        }

        constexpr redifference_iterator& operator+=(std::same_as<difference_type> auto n)
            requires std::random_access_iterator<It>
        {
            i_ += static_cast<std::iter_difference_t<It>>(n);
            return *this;
        }

        constexpr redifference_iterator& operator-=(std::same_as<difference_type> auto n)
            requires std::random_access_iterator<It>
        {
            i_ -= static_cast<std::iter_difference_t<It>>(n);
            return *this;
        }

        [[nodiscard]] constexpr decltype(auto) operator[](std::same_as<difference_type> auto n) const
            requires std::random_access_iterator<It>
        {
            return i_[static_cast<std::iter_difference_t<It>>(n)];
        }

        [[nodiscard]] friend constexpr bool operator==(const redifference_iterator& i, const redifference_iterator& j) {
            return i.i_ == j.i_;
        }

        template <std::same_as<difference_type> I> // TRANSITION, DevCom-10735214, should be abbreviated
        [[nodiscard]] friend constexpr redifference_iterator operator+(const redifference_iterator& it, I n)
            requires std::random_access_iterator<It>
        {
            return redifference_iterator{it.i_ + static_cast<std::iter_difference_t<It>>(n)};
        }

        template <std::same_as<difference_type> I> // TRANSITION, DevCom-10735214, should be abbreviated
        [[nodiscard]] friend constexpr redifference_iterator operator+(I n, const redifference_iterator& it)
            requires std::random_access_iterator<It>
        {
            return redifference_iterator{it.i_ + static_cast<std::iter_difference_t<It>>(n)};
        }

        template <std::same_as<difference_type> I> // TRANSITION, DevCom-10735214, should be abbreviated
        [[nodiscard]] friend constexpr redifference_iterator operator-(const redifference_iterator& it, I n)
            requires std::random_access_iterator<It>
        {
            return redifference_iterator{it.i_ - static_cast<std::iter_difference_t<It>>(n)};
        }

        [[nodiscard]] friend constexpr difference_type operator-(
            const redifference_iterator& i, const redifference_iterator& j)
            requires std::random_access_iterator<It>
        {
            return static_cast<Diff>(i.i_ - j.i_);
        }

        [[nodiscard]] friend constexpr auto operator<=>(const redifference_iterator& i, const redifference_iterator& j)
            requires std::random_access_iterator<It>
        {
            if constexpr (std::three_way_comparable<It>) {
                return i.i_ <=> j.i_;
            } else {
                if (i.i_ < j.i_) {
                    return std::weak_ordering::less;
                } else if (j.i_ < i.i_) {
                    return std::weak_ordering::greater;
                } else {
                    return std::weak_ordering::equivalent;
                }
            }
        }

        [[nodiscard]] friend constexpr bool operator<(const redifference_iterator& i, const redifference_iterator& j)
            requires std::random_access_iterator<It>
        {
            return i.i_ < j.i_;
        }

        [[nodiscard]] friend constexpr bool operator>(const redifference_iterator& i, const redifference_iterator& j)
            requires std::random_access_iterator<It>
        {
            return j.i_ < i.i_;
        }

        [[nodiscard]] friend constexpr bool operator<=(const redifference_iterator& i, const redifference_iterator& j)
            requires std::random_access_iterator<It>
        {
            return !(j.i_ < i.i_);
        }

        [[nodiscard]] friend constexpr bool operator>=(const redifference_iterator& i, const redifference_iterator& j)
            requires std::random_access_iterator<It>
        {
            return !(i.i_ < j.i_);
        }

        [[nodiscard]] constexpr const It& base() const noexcept {
            return i_;
        }

    private:
        It i_;
    };

    template <std::copyable S>
    struct redifference_sentinel {
        S se_;

        template <std::_Signed_integer_like Diff, class It>
            requires std::sentinel_for<S, It>
        [[nodiscard]] friend constexpr bool operator==(
            const redifference_iterator<Diff, It>& i, const redifference_sentinel& s) {
            return i.base() == s.se_;
        }

        template <std::_Signed_integer_like Diff, class It>
            requires std::sized_sentinel_for<S, It>
        [[nodiscard]] friend constexpr Diff operator-(
            const redifference_iterator<Diff, It>& i, const redifference_sentinel& s) {
            return static_cast<Diff>(i.base() - s.se_);
        }
        template <std::_Signed_integer_like Diff, class It>
            requires std::sized_sentinel_for<S, It>
        [[nodiscard]] friend constexpr Diff operator-(
            const redifference_sentinel& s, const redifference_iterator<Diff, It>& i) {
            return static_cast<Diff>(s.se_ - i.base());
        }
    };

    template <std::_Signed_integer_like Diff, ranges::borrowed_range Rng>
    [[nodiscard]] constexpr auto make_redifference_subrange(Rng&& r) {
        constexpr bool is_sized =
            ranges::sized_range<Rng> || std::sized_sentinel_for<ranges::sentinel_t<Rng>, ranges::iterator_t<Rng>>;
        using rediff_iter = redifference_iterator<Diff, ranges::iterator_t<Rng>>;
        using rediff_sent = redifference_sentinel<ranges::sentinel_t<Rng>>;

        if constexpr (is_sized) {
            const auto sz = to_unsigned(static_cast<Diff>(ranges::distance(r)));
            return ranges::subrange<rediff_iter, rediff_sent, ranges::subrange_kind::sized>{
                rediff_iter{ranges::begin(r)}, rediff_sent{ranges::end(r)}, sz};
        } else {
            return ranges::subrange<rediff_iter, rediff_sent, ranges::subrange_kind::unsized>{
                rediff_iter{ranges::begin(r)}, rediff_sent{ranges::end(r)}};
        }
    }
} // namespace test

template <class Category, class Element, test::Sized IsSized, test::CanDifference Diff, test::Common IsCommon,
    test::CanCompare Eq, test::ProxyRef Proxy, test::Copyability Copy>
constexpr bool std::ranges::enable_view<
    test::range<Category, Element, IsSized, Diff, IsCommon, Eq, Proxy, test::CanView::yes, Copy>> = true;

template <class T>
class basic_borrowed_range : public test::range<test::input, T, test::Sized::no, test::CanDifference::no,
                                 test::Common::no, test::CanCompare::no, test::ProxyRef::no> {
    using test::range<test::input, T, test::Sized::no, test::CanDifference::no, test::Common::no, test::CanCompare::no,
        test::ProxyRef::no>::range;
};

template <ranges::contiguous_range R>
basic_borrowed_range(R&) -> basic_borrowed_range<std::remove_reference_t<ranges::range_reference_t<R>>>;

template <class T>
constexpr bool ranges::enable_borrowed_range<::basic_borrowed_range<T>> = true;

template <int>
struct unique_tag {};

template <class I, int Tag = 0>
using ProjectionFor = unique_tag<Tag> (*)(std::iter_common_reference_t<I>);

template <class I>
using UnaryPredicateFor = boolish (*)(std::iter_common_reference_t<I>);

template <int Tag = 0>
using ProjectedUnaryPredicate = boolish (*)(unique_tag<Tag>);
template <class I2, int Tag1 = 0>
using HalfProjectedBinaryPredicateFor = boolish (*)(unique_tag<Tag1>, std::iter_common_reference_t<I2>);
template <int Tag1 = 0, int Tag2 = 0>
using ProjectedBinaryPredicate = boolish (*)(unique_tag<Tag1>, unique_tag<Tag2>);

template <class I1, class I2>
using BinaryPredicateFor = boolish (*)(std::iter_common_reference_t<I1>, std::iter_common_reference_t<I2>);

template <class Continuation, class Element>
struct with_output_iterators {
    template <class... Args>
    static constexpr void call() {
        using namespace test;
        using test::iterator;

        // Diff and Eq are not significant for "lone" single-pass iterators, so we can ignore them here.
        Continuation::template call<Args...,
            iterator<output, Element, CanDifference::no, CanCompare::no, ProxyRef::no>>();
        Continuation::template call<Args...,
            iterator<output, Element, CanDifference::no, CanCompare::no, ProxyRef::yes>>();
        // For forward and bidi, Eq is necessarily true but Diff and Proxy may vary.
        Continuation::template call<Args...,
            iterator<fwd, Element, CanDifference::no, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            iterator<fwd, Element, CanDifference::no, CanCompare::yes, ProxyRef::yes>>();
        Continuation::template call<Args...,
            iterator<fwd, Element, CanDifference::yes, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            iterator<fwd, Element, CanDifference::yes, CanCompare::yes, ProxyRef::yes>>();

        Continuation::template call<Args...,
            iterator<bidi, Element, CanDifference::no, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            iterator<bidi, Element, CanDifference::no, CanCompare::yes, ProxyRef::yes>>();
        Continuation::template call<Args...,
            iterator<bidi, Element, CanDifference::yes, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            iterator<bidi, Element, CanDifference::yes, CanCompare::yes, ProxyRef::yes>>();
        // Random iterators are Diff and Eq - only Proxy varies.
        Continuation::template call<Args...,
            iterator<random, Element, CanDifference::yes, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            iterator<random, Element, CanDifference::yes, CanCompare::yes, ProxyRef::yes>>();
        // Contiguous iterators are totally locked down.
        Continuation::template call<Args..., iterator<contiguous, Element>>();
    }
};

template <class Continuation, class Element>
struct with_writable_iterators {
    template <class... Args>
    static constexpr bool call() {
        using namespace test;
        using test::iterator;

        // Diff and Eq are not significant for "lone" single-pass iterators, so we can ignore them here.
        Continuation::template call<Args...,
            iterator<input, Element, CanDifference::no, CanCompare::no, ProxyRef::no>>();
        Continuation::template call<Args...,
            iterator<input, Element, CanDifference::no, CanCompare::no, ProxyRef::yes>>();

        with_output_iterators<Continuation, Element>::template call<Args...>();

        return true;
    }
};

template <class Continuation, class Element>
struct with_contiguous_ranges {
    template <class... Args>
    static constexpr void call() {
        using namespace test;
        using test::range;

        // Ditto always Eq; !IsSized && SizedSentinel is uninteresting (ranges::size still works), as is
        // !IsSized && IsCommon. contiguous also implies !Proxy.
        Continuation::template call<Args...,
            range<contiguous, Element, Sized::no, CanDifference::no, Common::no, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<contiguous, Element, Sized::yes, CanDifference::no, Common::no, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<contiguous, Element, Sized::yes, CanDifference::no, Common::yes, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<contiguous, Element, Sized::yes, CanDifference::yes, Common::no, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<contiguous, Element, Sized::yes, CanDifference::yes, Common::yes, CanCompare::yes, ProxyRef::no>>();
    }
};

template <class Continuation, class Element>
struct with_random_ranges {
    template <class... Args>
    static constexpr void call() {
        using namespace test;
        using test::range;

        // Ditto always Eq; !IsSized && SizedSentinel is uninteresting (ranges::size works either way), as is
        // !IsSized && IsCommon.
        Continuation::template call<Args...,
            range<random, Element, Sized::no, CanDifference::no, Common::no, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<random, Element, Sized::no, CanDifference::no, Common::no, CanCompare::yes, ProxyRef::yes>>();
        Continuation::template call<Args...,
            range<random, Element, Sized::yes, CanDifference::no, Common::no, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<random, Element, Sized::yes, CanDifference::no, Common::no, CanCompare::yes, ProxyRef::yes>>();
        Continuation::template call<Args...,
            range<random, Element, Sized::yes, CanDifference::no, Common::yes, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<random, Element, Sized::yes, CanDifference::no, Common::yes, CanCompare::yes, ProxyRef::yes>>();
        Continuation::template call<Args...,
            range<random, Element, Sized::yes, CanDifference::yes, Common::no, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<random, Element, Sized::yes, CanDifference::yes, Common::no, CanCompare::yes, ProxyRef::yes>>();
        Continuation::template call<Args...,
            range<random, Element, Sized::yes, CanDifference::yes, Common::yes, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<random, Element, Sized::yes, CanDifference::yes, Common::yes, CanCompare::yes, ProxyRef::yes>>();

        with_contiguous_ranges<Continuation, Element>::template call<Args...>();
    }
};

template <class Continuation, class Element>
struct with_bidirectional_ranges {
    template <class... Args>
    static constexpr void call() {
        using namespace test;
        using test::range;

        // Ditto always Eq; !IsSized && Diff is uninteresting (ranges::size still works).
        Continuation::template call<Args...,
            range<bidi, Element, Sized::no, CanDifference::no, Common::no, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<bidi, Element, Sized::no, CanDifference::no, Common::no, CanCompare::yes, ProxyRef::yes>>();
        Continuation::template call<Args...,
            range<bidi, Element, Sized::no, CanDifference::no, Common::yes, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<bidi, Element, Sized::no, CanDifference::no, Common::yes, CanCompare::yes, ProxyRef::yes>>();
        Continuation::template call<Args...,
            range<bidi, Element, Sized::yes, CanDifference::no, Common::no, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<bidi, Element, Sized::yes, CanDifference::no, Common::no, CanCompare::yes, ProxyRef::yes>>();
        Continuation::template call<Args...,
            range<bidi, Element, Sized::yes, CanDifference::no, Common::yes, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<bidi, Element, Sized::yes, CanDifference::no, Common::yes, CanCompare::yes, ProxyRef::yes>>();
        Continuation::template call<Args...,
            range<bidi, Element, Sized::yes, CanDifference::yes, Common::no, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<bidi, Element, Sized::yes, CanDifference::yes, Common::no, CanCompare::yes, ProxyRef::yes>>();
        Continuation::template call<Args...,
            range<bidi, Element, Sized::yes, CanDifference::yes, Common::yes, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<bidi, Element, Sized::yes, CanDifference::yes, Common::yes, CanCompare::yes, ProxyRef::yes>>();

        with_random_ranges<Continuation, Element>::template call<Args...>();
    }
};

template <class Continuation, class Element>
struct with_forward_ranges {
    template <class... Args>
    static constexpr void call() {
        using namespace test;
        using test::range;

        // forward always has Eq; !IsSized && Diff is uninteresting (sized_range is sized_range).
        Continuation::template call<Args...,
            range<fwd, Element, Sized::no, CanDifference::no, Common::no, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<fwd, Element, Sized::no, CanDifference::no, Common::no, CanCompare::yes, ProxyRef::yes>>();
        Continuation::template call<Args...,
            range<fwd, Element, Sized::no, CanDifference::no, Common::yes, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<fwd, Element, Sized::no, CanDifference::no, Common::yes, CanCompare::yes, ProxyRef::yes>>();
        Continuation::template call<Args...,
            range<fwd, Element, Sized::yes, CanDifference::no, Common::no, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<fwd, Element, Sized::yes, CanDifference::no, Common::no, CanCompare::yes, ProxyRef::yes>>();
        Continuation::template call<Args...,
            range<fwd, Element, Sized::yes, CanDifference::no, Common::yes, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<fwd, Element, Sized::yes, CanDifference::no, Common::yes, CanCompare::yes, ProxyRef::yes>>();
        Continuation::template call<Args...,
            range<fwd, Element, Sized::yes, CanDifference::yes, Common::no, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<fwd, Element, Sized::yes, CanDifference::yes, Common::no, CanCompare::yes, ProxyRef::yes>>();
        Continuation::template call<Args...,
            range<fwd, Element, Sized::yes, CanDifference::yes, Common::yes, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<fwd, Element, Sized::yes, CanDifference::yes, Common::yes, CanCompare::yes, ProxyRef::yes>>();

        with_bidirectional_ranges<Continuation, Element>::template call<Args...>();
    }
};

template <class Continuation, class Element>
struct with_input_ranges {
    template <class... Args>
    static constexpr void call() {
        using namespace test;
        using test::range;

        // For all ranges, IsCommon implies Eq.
        // For single-pass ranges, Eq is uninteresting without IsCommon (there's only one valid iterator
        // value at a time, and no reason to compare it with itself for equality).
        Continuation::template call<Args...,
            range<input, Element, Sized::no, CanDifference::no, Common::no, CanCompare::no, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<input, Element, Sized::no, CanDifference::no, Common::no, CanCompare::no, ProxyRef::yes>>();
        Continuation::template call<Args...,
            range<input, Element, Sized::no, CanDifference::no, Common::yes, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<input, Element, Sized::no, CanDifference::no, Common::yes, CanCompare::yes, ProxyRef::yes>>();

        Continuation::template call<Args...,
            range<input, Element, Sized::no, CanDifference::yes, Common::no, CanCompare::no, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<input, Element, Sized::no, CanDifference::yes, Common::no, CanCompare::no, ProxyRef::yes>>();
        Continuation::template call<Args...,
            range<input, Element, Sized::no, CanDifference::yes, Common::yes, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<input, Element, Sized::no, CanDifference::yes, Common::yes, CanCompare::yes, ProxyRef::yes>>();

        Continuation::template call<Args...,
            range<input, Element, Sized::yes, CanDifference::no, Common::no, CanCompare::no, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<input, Element, Sized::yes, CanDifference::no, Common::no, CanCompare::no, ProxyRef::yes>>();
        Continuation::template call<Args...,
            range<input, Element, Sized::yes, CanDifference::no, Common::yes, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<input, Element, Sized::yes, CanDifference::no, Common::yes, CanCompare::yes, ProxyRef::yes>>();

        Continuation::template call<Args...,
            range<input, Element, Sized::yes, CanDifference::yes, Common::no, CanCompare::no, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<input, Element, Sized::yes, CanDifference::yes, Common::no, CanCompare::no, ProxyRef::yes>>();
        Continuation::template call<Args...,
            range<input, Element, Sized::yes, CanDifference::yes, Common::yes, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<input, Element, Sized::yes, CanDifference::yes, Common::yes, CanCompare::yes, ProxyRef::yes>>();

        with_forward_ranges<Continuation, Element>::template call<Args...>();
    }
};

template <class Continuation, class Element>
struct with_output_ranges {
    template <class... Args>
    static constexpr void call() {
        using namespace test;
        using test::range;

        // For all ranges, IsCommon implies Eq.
        // For single-pass ranges, Eq is uninteresting without IsCommon (there's only one valid iterator
        // value at a time, and no reason to compare it with itself for equality).
        Continuation::template call<Args...,
            range<output, Element, Sized::no, CanDifference::no, Common::no, CanCompare::no, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<output, Element, Sized::no, CanDifference::no, Common::no, CanCompare::no, ProxyRef::yes>>();
        Continuation::template call<Args...,
            range<output, Element, Sized::no, CanDifference::no, Common::yes, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<output, Element, Sized::no, CanDifference::no, Common::yes, CanCompare::yes, ProxyRef::yes>>();

        Continuation::template call<Args...,
            range<output, Element, Sized::no, CanDifference::yes, Common::no, CanCompare::no, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<output, Element, Sized::no, CanDifference::yes, Common::no, CanCompare::no, ProxyRef::yes>>();
        Continuation::template call<Args...,
            range<output, Element, Sized::no, CanDifference::yes, Common::yes, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<output, Element, Sized::no, CanDifference::yes, Common::yes, CanCompare::yes, ProxyRef::yes>>();

        Continuation::template call<Args...,
            range<output, Element, Sized::yes, CanDifference::no, Common::no, CanCompare::no, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<output, Element, Sized::yes, CanDifference::no, Common::no, CanCompare::no, ProxyRef::yes>>();
        Continuation::template call<Args...,
            range<output, Element, Sized::yes, CanDifference::no, Common::yes, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<output, Element, Sized::yes, CanDifference::no, Common::yes, CanCompare::yes, ProxyRef::yes>>();

        Continuation::template call<Args...,
            range<output, Element, Sized::yes, CanDifference::yes, Common::no, CanCompare::no, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<output, Element, Sized::yes, CanDifference::yes, Common::no, CanCompare::no, ProxyRef::yes>>();
        Continuation::template call<Args...,
            range<output, Element, Sized::yes, CanDifference::yes, Common::yes, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<output, Element, Sized::yes, CanDifference::yes, Common::yes, CanCompare::yes, ProxyRef::yes>>();

        with_forward_ranges<Continuation, Element>::template call<Args...>();
    }
};

template <class Continuation, class Element>
struct with_input_or_output_ranges {
    template <class... Args>
    static constexpr void call() {
        using namespace test;
        using test::range;

        // For all ranges, IsCommon implies Eq.
        // For single-pass ranges, Eq is uninteresting without IsCommon (there's only one valid iterator
        // value at a time, and no reason to compare it with itself for equality).
        Continuation::template call<Args...,
            range<input, Element, Sized::no, CanDifference::no, Common::no, CanCompare::no, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<input, Element, Sized::no, CanDifference::no, Common::no, CanCompare::no, ProxyRef::yes>>();
        Continuation::template call<Args...,
            range<input, Element, Sized::no, CanDifference::no, Common::yes, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<input, Element, Sized::no, CanDifference::no, Common::yes, CanCompare::yes, ProxyRef::yes>>();

        Continuation::template call<Args...,
            range<input, Element, Sized::no, CanDifference::yes, Common::no, CanCompare::no, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<input, Element, Sized::no, CanDifference::yes, Common::no, CanCompare::no, ProxyRef::yes>>();
        Continuation::template call<Args...,
            range<input, Element, Sized::no, CanDifference::yes, Common::yes, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<input, Element, Sized::no, CanDifference::yes, Common::yes, CanCompare::yes, ProxyRef::yes>>();

        Continuation::template call<Args...,
            range<input, Element, Sized::yes, CanDifference::no, Common::no, CanCompare::no, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<input, Element, Sized::yes, CanDifference::no, Common::no, CanCompare::no, ProxyRef::yes>>();
        Continuation::template call<Args...,
            range<input, Element, Sized::yes, CanDifference::no, Common::yes, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<input, Element, Sized::yes, CanDifference::no, Common::yes, CanCompare::yes, ProxyRef::yes>>();

        Continuation::template call<Args...,
            range<input, Element, Sized::yes, CanDifference::yes, Common::no, CanCompare::no, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<input, Element, Sized::yes, CanDifference::yes, Common::no, CanCompare::no, ProxyRef::yes>>();
        Continuation::template call<Args...,
            range<input, Element, Sized::yes, CanDifference::yes, Common::yes, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            range<input, Element, Sized::yes, CanDifference::yes, Common::yes, CanCompare::yes, ProxyRef::yes>>();

        with_output_ranges<Continuation, Element>::template call<Args...>();
    }
};

template <class Continuation, class Element>
struct with_input_iterators {
    template <class... Args>
    static constexpr void call() {
        using namespace test;
        using test::iterator;

        // IsSized and Eq are not significant for "lone" single-pass iterators, so we can ignore them here.
        Continuation::template call<Args...,
            iterator<input, Element, CanDifference::no, CanCompare::no, ProxyRef::no>>();
        Continuation::template call<Args...,
            iterator<input, Element, CanDifference::no, CanCompare::no, ProxyRef::yes>>();
        // For forward and bidi, Eq is necessarily true but IsSized and Proxy may vary.
        Continuation::template call<Args...,
            iterator<fwd, Element, CanDifference::no, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            iterator<fwd, Element, CanDifference::no, CanCompare::yes, ProxyRef::yes>>();
        Continuation::template call<Args...,
            iterator<fwd, Element, CanDifference::yes, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            iterator<fwd, Element, CanDifference::yes, CanCompare::yes, ProxyRef::yes>>();

        Continuation::template call<Args...,
            iterator<bidi, Element, CanDifference::no, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            iterator<bidi, Element, CanDifference::no, CanCompare::yes, ProxyRef::yes>>();
        Continuation::template call<Args...,
            iterator<bidi, Element, CanDifference::yes, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            iterator<bidi, Element, CanDifference::yes, CanCompare::yes, ProxyRef::yes>>();
        // Random iterators are IsSized and Eq - only Proxy varies.
        Continuation::template call<Args...,
            iterator<random, Element, CanDifference::yes, CanCompare::yes, ProxyRef::no>>();
        Continuation::template call<Args...,
            iterator<random, Element, CanDifference::yes, CanCompare::yes, ProxyRef::yes>>();
        // Contiguous iterators are totally locked down.
        Continuation::template call<Args..., iterator<contiguous, Element>>();
    }
};

template <class Instantiator, class Element>
constexpr void test_out() {
    with_output_ranges<Instantiator, Element>::call();
}

template <class Instantiator, class Element>
constexpr void test_in() {
    with_input_ranges<Instantiator, Element>::call();
}

template <class Instantiator, class Element>
constexpr void test_inout() {
    with_input_or_output_ranges<Instantiator, Element>::call();
}

template <class Instantiator, class Element>
constexpr void test_fwd() {
    with_forward_ranges<Instantiator, Element>::call();
}

template <class Instantiator, class Element>
constexpr void test_bidi() {
    with_bidirectional_ranges<Instantiator, Element>::call();
}

template <class Instantiator, class Element>
constexpr void test_random() {
    with_random_ranges<Instantiator, Element>::call();
}

template <class Instantiator, class Element>
constexpr void test_contiguous() {
    with_contiguous_ranges<Instantiator, Element>::call();
}

template <class Instantiator, class Element1, class Element2>
constexpr void test_in_in() {
    with_input_ranges<with_input_ranges<Instantiator, Element2>, Element1>::call();
}

template <class Instantiator, class Element1, class Element2>
constexpr void test_in_fwd() {
    with_input_ranges<with_forward_ranges<Instantiator, Element2>, Element1>::call();
}

template <class Instantiator, class Element1, class Element2>
constexpr void test_in_random() {
    with_input_ranges<with_random_ranges<Instantiator, Element2>, Element1>::call();
}

template <class Instantiator, class Element1, class Element2>
constexpr void test_fwd_fwd() {
    with_forward_ranges<with_forward_ranges<Instantiator, Element2>, Element1>::call();
}

template <class Instantiator, class Element1, class Element2>
constexpr void test_bidi_bidi() {
    with_bidirectional_ranges<with_bidirectional_ranges<Instantiator, Element2>, Element1>::call();
}

template <class Instantiator, class Element1, class Element2>
constexpr void input_range_output_iterator_permutations() {
    with_input_ranges<with_output_iterators<Instantiator, Element2>, Element1>::call();
}

template <class Instantiator, class Element1, class Element2>
constexpr void test_in_write() {
    with_input_ranges<with_writable_iterators<Instantiator, Element2>, Element1>::call();
}

template <class Instantiator, class Element1, class Element2>
constexpr void test_fwd_write() {
    with_forward_ranges<with_writable_iterators<Instantiator, Element2>, Element1>::call();
}

template <class Instantiator, class Element1, class Element2>
constexpr void test_bidi_write() {
    with_bidirectional_ranges<with_writable_iterators<Instantiator, Element2>, Element1>::call();
}

template <class Instantiator, class Element1, class Element2>
constexpr void test_contiguous_write() {
    with_contiguous_ranges<with_writable_iterators<Instantiator, Element2>, Element1>::call();
}

template <class Instantiator, class Element>
constexpr void test_read() {
    with_input_iterators<Instantiator, Element>::call();
}

template <class Instantiator, class Element1, class Element2>
constexpr void test_read_write() {
    with_input_iterators<with_writable_iterators<Instantiator, Element2>, Element1>::call();
}

template <class Instantiator, class Element1, class Element2, class Element3>
constexpr void test_in_in_write() {
    with_input_ranges<with_input_ranges<with_writable_iterators<Instantiator, Element3>, Element2>, Element1>::call();
}

template <std::size_t I>
struct get_nth_fn {
    template <class T>
    [[nodiscard]] constexpr auto&& operator()(T&& t) const noexcept
        requires requires { get<I>(std::forward<T>(t)); }
    {
        return get<I>(std::forward<T>(t));
    }

    template <class T, class Elem>
    [[nodiscard]] constexpr decltype(auto) operator()(test::proxy_reference<T, Elem> r) const noexcept
        requires requires { (*this)(r.peek()); }
    {
        return (*this)(r.peek());
    }
};
inline constexpr get_nth_fn<0> get_first;
inline constexpr get_nth_fn<1> get_second;

template <class R>
concept CanBegin = requires(R&& r) { ranges::begin(std::forward<R>(r)); };
template <class R>
concept CanMemberBegin = requires(R&& r) { std::forward<R>(r).begin(); };

template <class R>
concept CanEnd = requires(R&& r) { ranges::end(std::forward<R>(r)); };
template <class R>
concept CanMemberEnd = requires(R&& r) { std::forward<R>(r).end(); };

template <class R>
concept CanCBegin = requires(R&& r) { ranges::cbegin(std::forward<R>(r)); };
template <class R>
concept CanMemberCBegin = requires(R&& r) { std::forward<R>(r).cbegin(); };

template <class R>
concept CanCEnd = requires(R&& r) { ranges::cend(std::forward<R>(r)); };
template <class R>
concept CanMemberCEnd = requires(R&& r) { std::forward<R>(r).cend(); };

template <class R>
concept CanRBegin = requires(R&& r) { ranges::rbegin(std::forward<R>(r)); };
template <class R>
concept CanREnd = requires(R&& r) { ranges::rend(std::forward<R>(r)); };

template <class R>
concept CanCRBegin = requires(R&& r) { ranges::crbegin(std::forward<R>(r)); };
template <class R>
concept CanCREnd = requires(R&& r) { ranges::crend(std::forward<R>(r)); };

template <class R>
concept CanEmpty = requires(R&& r) { ranges::empty(std::forward<R>(r)); };

template <class R>
concept CanSize = requires(R&& r) { ranges::size(std::forward<R>(r)); };
template <class R>
concept CanMemberSize = requires(R&& r) { std::forward<R>(r).size(); };

template <class R>
concept CanSSize = requires(R&& r) { ranges::ssize(std::forward<R>(r)); };

template <class R>
concept CanData = requires(R&& r) { ranges::data(std::forward<R>(r)); };
template <class R>
concept CanMemberData = requires(R&& r) { std::forward<R>(r).data(); };

template <class R>
concept CanCData = requires(R&& r) { ranges::cdata(std::forward<R>(r)); };

template <class T>
concept CanMemberBase = requires(T&& t) { std::forward<T>(t).base(); };

template <class R>
concept CanMemberEmpty = requires(R&& r) { std::forward<R>(r).empty(); };

template <class R>
concept CanMemberFront = requires(R&& r) { std::forward<R>(r).front(); };
template <class R>
concept CanMemberBack = requires(R&& r) { std::forward<R>(r).back(); };

template <class R>
concept CanIndex = requires(R&& r, const ranges::range_difference_t<R> i) { std::forward<R>(r)[i]; };

template <class R>
concept CanBool = requires(R&& r) { std::forward<R>(r) ? true : false; };

template <class I>
concept CanIterSwap = requires(I&& i1, I&& i2) { ranges::iter_swap(std::forward<I>(i1), std::forward<I>(i2)); };
