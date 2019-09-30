/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2017-2019. All rights reserved.
 * iSulad licensed under the Mulan PSL v1.
 * You can use this software according to the terms and conditions of the Mulan PSL v1.
 * You may obtain a copy of Mulan PSL v1 at:
 *     http://license.coscl.org.cn/MulanPSL
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v1 for more details.
 * Author: tanyifeng
 * Create: 2017-11-22
 * Description: provide err function definition
 *********************************************************************************/
#ifndef _ERRORS_H_
#define _ERRORS_H_

#include <string>
#include <vector>

class Errors {
public:
    Errors();
    Errors(const Errors &copy) : m_message(copy.m_message), m_code(copy.m_code) {}
    Errors &operator=(const Errors &);
    virtual ~Errors();

    void Clear();
    std::string &GetMessage();
    const char *GetCMessage() const;
    int GetCode() const;
    bool Empty() const;
    bool NotEmpty() const;

    void AppendError(const std::string &msg);
    void SetError(const std::string &msg);
    void SetError(const char *msg);
    void Errorf(const char *fmt, ...);

    void SetAggregate(const std::vector<std::string> &msgs);

private:
    std::string m_message;
    int m_code;
};

#endif /* _ERRORS_H_ */
