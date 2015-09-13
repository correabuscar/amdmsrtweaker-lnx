/*
 * Copyright (c) Martin Kinkelin
 *
 * See the "License.txt" file in the root directory for infos
 * about permitted and prohibited uses of this code.
 */

#pragma once

#include <vector>
#include "Info.h"


class Worker {
public:

    Worker(const Info& info)
        : _info(&info)
    { }

    void ParseParams();

    void ApplyChanges();


private:

    const Info* _info;
    std::vector<PStateInfo> _pStates;
};
