#ifndef SINGLETON_H
#define SINGLETON_H

template <typename T>
class Singleton
{
    friend T;

public:
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;
    Singleton(Singleton&&) = delete;
    Singleton& operator=(Singleton&&) = delete;

    static T& GetInstance()
    {
        static T instance;
        return instance;
    }

private:
    Singleton() = default;
    ~Singleton() = default;
};



#endif // SINGLETON_H
