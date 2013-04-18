#ifndef IVW_BASEOPTIONPROPERTY_H
#define IVW_BASEOPTIONPROPERTY_H


#include <inviwo/core/properties/stringproperty.h>
#include <inviwo/core/common/inviwocoredefine.h>
#include <inviwo/core/common/inviwo.h>

namespace inviwo {

class IVW_CORE_API BaseOptionProperty : public StringProperty{
public:

    BaseOptionProperty(std::string identifier, std::string displayName, std::string value,
        PropertyOwner::InvalidationLevel invalidationLevel,
        PropertySemantics::Type semantics = PropertySemantics::Default)
        :StringProperty(identifier,displayName,value,invalidationLevel,semantics)
    {}

    virtual std::vector< std::string > getOptionKeys()=0;
    virtual int getSelectedOption()=0;
    virtual void setSelectedOption( std::string )=0;
    virtual void updateValue( std::string )=0;

};


template<typename T>
class IVW_CORE_API TemplatedOptionProperty : public BaseOptionProperty {

public:
    TemplatedOptionProperty(std::string identifier, std::string displayName, std::string value,
                            PropertyOwner::InvalidationLevel invalidationLevel=PropertyOwner::INVALID_OUTPUT,
                            PropertySemantics::Type semantics = PropertySemantics::Default);

    virtual void addOption(std::string optionName,T optionValue);
    virtual std::vector< std::pair<std::string, T> > getOptions();

    std::vector< std::string > getOptionKeys();
    int getSelectedOption();
    T getSelectedValue();
    void setSelectedOption( std::string );
    virtual void updateValue( std::string);

private:
    std::vector< std::pair<std::string, T> > optionVector_;
};


template <typename T>
TemplatedOptionProperty<T>::TemplatedOptionProperty(std::string identifier, std::string displayName, std::string value,
                                                    PropertyOwner::InvalidationLevel invalidationLevel,
                                                    PropertySemantics::Type semantics )
    : BaseOptionProperty(identifier, displayName, value, invalidationLevel, semantics)
{}

template<typename T>
void TemplatedOptionProperty<T>::addOption(std::string optionName,T optionValue) {
    optionVector_.push_back(std::make_pair(optionName,optionValue));
}
template<typename T>
std::vector< std::pair<std::string, T> > TemplatedOptionProperty< T >::getOptions() {
    return optionVector_;
}

template<typename T>
std::vector< std::string > TemplatedOptionProperty<T>::getOptionKeys(){
    
    std::vector< std::string > ret;
    size_t size = optionVector_.size();
    for (size_t i=0; i<size;i++) {
        ret.push_back(optionVector_.at(i).first);
    }
    return ret;
}

template<typename T>
int TemplatedOptionProperty<T>::getSelectedOption() {
    std::vector< std::string > tmp = getOptionKeys();
    int size = static_cast<int>(tmp.size());
    for (int i=0; i<size;i++) {
        if(getOptionKeys().at(i)== value_)
            return i;
    }
    return 0;
}

template<typename T>
T TemplatedOptionProperty<T>::getSelectedValue() {
    return optionVector_[getSelectedOption()].second;
}

template<typename T>
void TemplatedOptionProperty<T>::setSelectedOption(std::string tmpStr) {
    set(tmpStr);
}

template<typename T>
void TemplatedOptionProperty<T>::updateValue(std::string tmpStr) {
    set(tmpStr);
}

} // namespace

#endif // IVW_BASEOPTIONPROPERTY_H