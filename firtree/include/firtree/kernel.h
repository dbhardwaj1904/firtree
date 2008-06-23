// FIRTREE - A generic image processing library
// Copyright (C) 2007, 2008 Rich Wareham <richwareham@gmail.com>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License verstion as published
// by the Free Software Foundation.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc., 51
// Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA

//=============================================================================
/// \file kernel.h The interface to a FIRTREE kernel.
//=============================================================================

//=============================================================================
#ifndef FIRTREE_KERNEL_H
#define FIRTREE_KERNEL_H
//=============================================================================

#include <stdlib.h>
#include <string>
#include <vector>
#include <map>

#include <firtree/main.h>
#include <firtree/math.h>

namespace Firtree {

class NumericParameter;
class SamplerParameter;
class Kernel;

//=============================================================================
/// The base class for all kernel parameters.
class Parameter : public ReferenceCounted
{
    public:
        ///@{
        /// Protected constructors. Use the static factory methods of 
        /// derived classes.
        Parameter();
        Parameter(const Parameter& p);
        virtual ~Parameter();
        ///@}
};

//=============================================================================
/// A numeric (scalar, vector or colour) parameter for a kernel.
class NumericParameter : public Parameter
{
    public:
        /// An enum specifying the possible base types for elements in 
        /// a numeric value.
        enum BaseType {
            TypeInteger,    ///< Integer elements.
            TypeFloat,      ///< Floating point elements.
            TypeBool,       ///< Boolean elements.
        };

    protected:
        ///@{
        /// Protected constructors/destructors. Use the Create()
        /// static method.
        NumericParameter();
        ///@}

    public:
        virtual ~NumericParameter();
        // ====================================================================
        // CONSTRUCTION METHODS

        /// Create a numeric parameter.
        static Parameter* Create();

        // ====================================================================
        // CONST METHODS

        ///@{
        /// Return the value of the 'idx'th element as a float, integer or
        /// boolean.
        float GetFloatValue(int idx) const { return m_Value[idx].f; }
        int GetIntValue(int idx) const { return m_Value[idx].i; }
        bool GetBoolValue(int idx) const { return m_Value[idx].i != 0; }
        ///@}

        /// Return the base type of the numeric parameter's elements.
        BaseType GetBaseType() const { return m_BaseType; }

        /// Return the size of the numeric parameter. A scalar has size 1
        /// and an n-D vector has size n. The size must be between 1 and
        /// 4 inclusive.
        int GetSize() const { return m_Size; }

        /// Return a boolean indicating if the parameter represents a 
        /// color.
        bool IsColor() const { return m_IsColor; }

        // ====================================================================
        // MUTATING METHODS

        ///@{
        /// Assign the 'idx'th element a floating point, integer or boolean
        /// value.
        void SetFloatValue(float f, int idx) { m_Value[idx].f = f; }
        void SetIntValue(int i, int idx) { m_Value[idx].i = i; }
        void SetBoolValue(bool b, int idx) { m_Value[idx].i = b ? 1 : 0; }
        ///@}

        /// Set the base type of the parameter's elements.
        void SetBaseType(BaseType bt) { m_BaseType = bt; }

        /// Set the size of the numeric parameter.
        /// \see GetSize().
        void SetSize(int s) { m_Size = s; }

        /// Set a flag indicating whether this parameter represents a 
        /// color.
        void SetIsColor(bool flag) { m_IsColor = flag; }

        /// Copy the value of the numeric parameter passed.
        void AssignFrom(const NumericParameter& param);

    private:
        BaseType    m_BaseType;
        int         m_Size;
        union {
            float f;
            int i;      /* 0/1 for bool */
        }           m_Value[4];
        bool        m_IsColor : 1;
};

//=============================================================================
/// Abstract base class for a sampler parameter.
class SamplerParameter : public Parameter
{
    protected:
        SamplerParameter();

    public:
        virtual ~SamplerParameter();
        /// Return a rectangle in sampler co-ordinates which completely
        /// encloses the non-transparent pixels of the source.
        virtual const Rect2D GetDomain() const = 0;

        /// Return a rectangle in world co-ordinates which completely
        /// encloses the non-transparent pixels of the source.
        virtual const Rect2D GetExtent() const = 0;

        /// Return a pointer to the affine transform which maps the
        /// sampler co-ordinate system to the world co-ordinate system.
        virtual const AffineTransform* GetTransform() const = 0;
};

//=============================================================================
/// A kernel encapsulates a kernel function written in the FIRTREE kernel
/// language along with a set of values for various kernel parameters.
class Kernel : public ReferenceCounted
{
    protected:
        ///@{
        /// Protected constructors/destructors. Use the various Kernel
        /// factory functions instead.
        Kernel(const char* source);
        ///@}

    public:
        virtual ~Kernel();
        // ====================================================================
        // CONST METHODS

        /// Retrieve a pointer to the source for this kernel expressed in
        /// the FIRTREE kernel language.
        virtual const char* GetSource() const = 0;

        // ====================================================================
        // MUTATING METHODS

        /// Return a const reference to a map containing the parameter 
        /// name/value pairs.
        virtual const std::map<std::string, Parameter*>& GetParameters() = 0;

        /// Accessor for setting a kernel parameter value. The accessors below
        /// are convenience wrappers around this method.
        virtual void SetValueForKey(Parameter* parameter, const char* key) = 0;

        ///@{
        /// Accessors for the various scalar, vector and sampler parameters.
        void SetValueForKey(float value, const char* key);
        void SetValueForKey(const float* value, int count, 
                const char* key);
        void SetValueForKey(int value, const char* key);
        void SetValueForKey(const int* value, int count, 
                const char* key);
        void SetValueForKey(bool value, const char* key);
        void SetValueForKey(const bool* value, int count, 
                const char* key);
        ///@}
    private:
};

}

//=============================================================================
#endif // FIRTREE_KERNEL_H
//=============================================================================

//=============================================================================
// vim:sw=4:ts=4:cindent:et
