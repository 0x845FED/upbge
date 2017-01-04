/** \file gameengine/Expressions/Expression.cpp
 *  \ingroup expressions
 */
// Expression.cpp: implementation of the CExpression class.
/*
 * Copyright (c) 1996-2000 Erwin Coumans <coockie@acm.org>
 *
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.  Erwin Coumans makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 */

#include "EXP_Expression.h"
#include "EXP_ErrorValue.h"

CExpression::CExpression()
	:m_refcount(1)
{
}

CExpression::~CExpression()
{
	BLI_assert(m_refcount == 0);
}

CExpression *CExpression::AddRef()
{
	++m_refcount;
	return this;
}

CExpression *CExpression::Release()
{
	if (--m_refcount < 1) {
		delete this;
		return NULL;
	}
	return this;
}
