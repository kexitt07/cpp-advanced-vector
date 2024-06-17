#pragma once
#include <iostream>
#include <cassert>
#include <cstdlib>
#include <new>
#include <utility>
#include <stdexcept>
#include <memory>
#include <type_traits>

template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity) {
    }

    RawMemory(const RawMemory& other) = delete;
    RawMemory(RawMemory&& other) {
        this->Swap(other);
    }

    RawMemory& operator=(const RawMemory& rhs) = delete;
    RawMemory& operator=(RawMemory&& rhs) {
        if (this != &rhs) {
            this->Swap(rhs);
        }
        return *this;
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    T* operator+(size_t offset) noexcept {
        // Разрешается получать адрес ячейки памяти, следующей за последним элементом массива
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<RawMemory&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept {
        return buffer_;
    }

    T* GetAddress() noexcept {
        return buffer_;
    }

    size_t Capacity() const {
        return capacity_;
    }

private:
    // Выделяет сырую память под n элементов и возвращает указатель на неё
    static T* Allocate(size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    // Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    size_t capacity_ = 0;
};

template <typename T>
class Vector {
public:
    using iterator = T*;
    using const_iterator = const T*;

    Vector() = default;

    Vector(size_t size)
        : data_(size)
        , size_(size)
    {
        std::uninitialized_value_construct_n(data_.GetAddress(), size_);
    }

    Vector(const Vector& other)
        : data_(other.Size())
        , size_(other.Size())
    {
        std::uninitialized_copy_n(other.data_.GetAddress(), size_, data_.GetAddress());
    }

    Vector(Vector&& rhs) {
        this->Swap(rhs);
    }

    ~Vector() {
        std::destroy_n(data_.GetAddress(), size_);
    }

    void Resize(size_t new_size) {
        if (new_size == size_) {
            return;
        } else if (new_size > size_) {
            if (new_size > this->Capacity()) {
                Reserve(new_size);
            }
            std::uninitialized_value_construct_n(data_.GetAddress() + size_
                                                , new_size - size_);
        } else {
            std::destroy_n(data_.GetAddress() + new_size, size_ - new_size);
        }
        size_ = new_size;
    }

    void PushBack(const T& value) {
        EmplaceBack(value);
    }

    void PushBack(T&& value) {
        EmplaceBack(std::move(value));        
    }

    template <typename... Args>
    T& EmplaceBack(Args&&... args) {
        if (size_ == this->Capacity()) {
            RawMemory<T> buffer(size_ == 0 ? 1 : size_ * 2);
            new (&buffer[size_]) T(std::forward<Args>(args)...);

            MoveOrCopyAndSwap(data_, size_, buffer);

        } else {
            new (&data_[size_]) T(std::forward<Args>(args)...);
        }
        ++size_;
        return (*this)[size_-1];
    }

    void PopBack() noexcept {
        if (size_ > 0) {
            data_[size_ - 1].~T();
            --size_;
        }
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= data_.Capacity()) {
            return;
        }
        RawMemory<T> buffer(new_capacity);
        MoveOrCopyAndSwap(data_, size_, buffer);
    }

    void Swap(Vector& other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        assert(pos >= this->begin() && pos <= this->end());

        size_t dist = pos - this->begin();
        iterator position = this->begin() + dist;

        if (size_ == this->Capacity()) {
            RawMemory<T> buffer(size_ == 0 ? 1 : size_ * 2);
            position = buffer.GetAddress() + dist;
            new (position) T(std::forward<Args>(args)...);

            MoveOrCopy(data_.GetAddress(), dist, buffer.GetAddress());
            MoveOrCopy(data_.GetAddress() + dist, size_ - dist, buffer.GetAddress() + (dist+1));
            std::destroy_n(data_.GetAddress(), size_);

            data_.Swap(buffer);

        } else {
            if (dist == size_) {
                new (position) T(std::forward<Args>(args)...);
            } else {
                T* temp = new T(std::forward<Args>(args)...);
                new (this->end()) T(std::move(data_[size_  - 1]));
                std::move_backward(position, this->end()-1, this->end());
                position->~T();
                data_[dist] = std::move(*temp);
            }
        }

        ++size_;
        return position;
    }

    iterator Insert(const_iterator pos, const T& value) {
        return Emplace(pos, value);
    }

    iterator Insert(const_iterator pos, T&& value) {
        return Emplace(pos, std::move(value));
    }

    iterator Erase(const_iterator pos) {
        assert(pos >= this->begin() && pos <= this->end());

        size_t dist = pos - data_.GetAddress();
        iterator position = data_.GetAddress() + dist;

        position->~T();
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::move(position + 1, this->end(), position);
        } else {
            std::copy(position + 1, this->end(), position);
        }

        --size_;
        return position;
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

    Vector& operator=(const Vector& rhs) {
        if (this != &rhs) {
            if (rhs.size_ > data_.Capacity()) {
                Vector temp(rhs);
                this->Swap(temp);
            } else {
                CopyToFilledVector(*this, rhs);
            }
        }
        return *this;
    }

    Vector& operator=(Vector&& other) {
        if (this != &other) {
            this->Swap(other);
        }
        return *this;
    }

    iterator begin() noexcept {
        return data_.GetAddress();
    }
    iterator end() noexcept {
        return data_.GetAddress() + size_;
    }
    const_iterator begin() const noexcept {
        return data_.GetAddress();
    }
    const_iterator end() const noexcept {
        return data_.GetAddress() + size_;
    }
    const_iterator cbegin() const noexcept {
        return this->begin();
    }
    const_iterator cend() const noexcept {
        return this->end();
    }

private:
    RawMemory<T> data_;
    size_t size_ = 0;

private:
    void CopyToFilledVector(Vector& lhs, const Vector& rhs) {
        const size_t& lhs_size = lhs.size_;
        const size_t& rhs_size = rhs.Size();

        const size_t min_size = std::min(lhs_size, rhs_size);
        std::copy(rhs.begin(), rhs.end(), lhs.begin());
        if (min_size == lhs_size) {
            std::uninitialized_copy_n(rhs.data_.GetAddress() + lhs_size
                                    , rhs_size - lhs_size
                                    , lhs.data_.GetAddress() + lhs_size);
        }
        if (min_size == rhs_size && rhs_size != lhs_size) {
            std::destroy_n(lhs.data_.GetAddress() + rhs_size, lhs_size - rhs_size);
        }
        lhs.size_ = rhs_size;
    }

    void MoveOrCopy(T* src, size_t n, T* dest) {
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(src, n, dest);
        } else {
            std::uninitialized_copy_n(src, n, dest);
        }
    }

    void MoveOrCopyAndSwap(RawMemory<T>& data, size_t n_elems, RawMemory<T>& buf) {
        MoveOrCopy(data.GetAddress(), n_elems, buf.GetAddress());
        std::destroy_n(data.GetAddress(), n_elems);
        data.Swap(buf);
    }
};