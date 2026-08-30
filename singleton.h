#ifndef SINGLETON_H
#define SINGLETON_H

#include "global.h"

template <typename T>
class TSingLeton
{
protected:
    TSingLeton() = default;
    TSingLeton(const TSingLeton<T>&) = delete;
    TSingLeton& operator=(const TSingLeton<T>& st) = delete;

    ~TSingLeton()
    {
        std::cout << "singleton destruct" << std::endl;
    }

public:
    static T& GetInstance()
    {
        static T instance;
        return instance;
    }
};



#endif // SINGLETON_H
