//
//  Attributes.hpp
//  A4N
//
//  Created by Klaus Ahrens on 20.09.21.
//

#ifndef Attributes_h
#define Attributes_h
#include <cstddef>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Attributes {

using index = size_t;

template <typename T>
class NodeAttribute;

// Base class for all node attributes.
class NodeAttributeStorageBase {
public:
    NodeAttributeStorageBase(std::string name, std::type_index type)
    : name{name}, type{type} { }
    
    virtual ~NodeAttributeStorageBase() = default;
    
    virtual void invalidateAttributes() = 0;
    
    std::string_view getName() {
        return name;
    }
    
    std::type_index getType() {
        return type;
    }
    
    bool isValid(index i) {
        return i < valid.size() && valid[i];
    }
    
    // Called by Graph when node is deleted.
    void invalidate(index i) {
        if(i < valid.size()) {
            valid[i] = false;
        }
        --validElements;
    }
    
protected:
    void markValid(index i) {
        if(i >= valid.size()) {
            valid.resize(i + 1);
        }
        valid[i] = true;
        ++validElements;
    }
    
    void checkIndex(index i) {
        if (!isValid(i)) {
            throw std::runtime_error("Invalid attribute value");
        }
    }
    
private:
    std::string name;
    std::type_index type;
    std::vector<bool> valid; // For each node: whether attribute is set or not.
protected:
    index validElements = 0;
}; // class NodeAttributeStorageBase

template<typename T>
class NodeAttributeStorage : public NodeAttributeStorageBase {
public:
    NodeAttributeStorage(std::string name)
    : NodeAttributeStorageBase{std::move(name), typeid(T)} { }
    
    ~NodeAttributeStorage() override {
        invalidateAttributes();
        // std::cerr<<"storage deleted\n";
    }
    
    void invalidateAttributes() override {
        for (auto att: attrSet) att->invalidateAttribute();
    }
    
    void resize(index i) {
        if(i >= values.size()) {
            values.resize(i + 1);
        }
    }
    
    auto size() {
        return validElements;
    }
    
    void set(index i, T v) {
        resize(i);
        values[i] = std::move(v);
        markValid(i);
    }
    
    std::optional<T> get(index i) {
        if(i >= values.size() || !isValid(i)) {
            return std::nullopt;
        }
        return values[i];
    }
private:
    std::vector<T> values;
    friend class NodeAttribute<T>;
    std::unordered_set<NodeAttribute<T>*> attrSet;
}; // class NodeAttributeStorage<T>

template<typename T>
class NodeAttribute {
    
    class Iterator {
    public:
        Iterator(NodeAttributeStorage<T> *storage)
        : storage{storage}, idx{0} {
            if (storage) {
                nextValid();
            }
        }
        
        Iterator& nextValid() {
            while (storage && !storage->isValid(idx)) {
                if (idx >= storage->values.size()) {
                    storage = nullptr;
                    return *this;
                }
                ++idx;
            }
            return *this;
        }
        
        Iterator& operator++() {
            if (!storage) {
                throw std::runtime_error("Invalid attribute iterator");
            }
            ++idx;
            return nextValid();
        }
        
        T& operator*() {
            if (!storage) {
                throw std::runtime_error("Invalid attribute iterator");
            }
            return storage->values[idx];
        }
        
        auto nodeValuePair() {
            return std::make_pair(idx, **this);
        }
        
        bool operator==(Iterator const& iter) {
            if (storage == nullptr && iter.storage == nullptr) {
                return true;
            }
            return storage != nullptr && idx==iter.idx;
        }
        
        bool operator!=(Iterator const& iter) {
            return !(*this==iter);
        }
    private:
        NodeAttributeStorage<T>* storage;
        index idx;
    }; // class Iterator
    
    class IndexProxy {
    public:
        IndexProxy(NodeAttributeStorage<T> *storage, index idx)
        : storage{storage}, idx{idx} {}
        
        // reading at idx
        operator T() {
            storage->checkIndex(idx);
            return storage->values[idx];
        }
        
        // writing at idx
        T& operator=(T const& other) {
            storage->set(idx, std::move(other));
            return storage->values[idx];
        }
    private:
        NodeAttributeStorage<T>* storage;
        index idx;
    }; // class IndexProxy
public:
    explicit NodeAttribute(std::shared_ptr<NodeAttributeStorage<T>> owned_storage)
    : owned_storage{owned_storage}, valid{true} {
        // std::cerr<<"NodeAttribute ("<<owned_storage->getName()<<") at "<<this<<"\n";
        owned_storage->attrSet.insert(this);
    }
    
    NodeAttribute(NodeAttribute const& other)
    : owned_storage{other.owned_storage}, valid{other.valid} {
        owned_storage->attrSet.insert(this);
    }
    
    ~NodeAttribute() {
        owned_storage->attrSet.erase(this);
        // std::cerr<<"NodeAttribute ("<<owned_storage->getName()<<") at "<<this<<" deleted\n";
    }
    
    auto begin() {
        return Iterator(owned_storage.get()).nextValid();
    }
    
    auto end() {
        return Iterator(nullptr);
    }
    
    auto size() {
        return owned_storage->size();
    }
    
    void set(index i, T v) {
        checkAttribute();
        return owned_storage->set(i, std::move(v));
    }
    
    auto get(index i) {
        checkAttribute();
        return owned_storage->get(i);
    }
    
    IndexProxy operator[](index i) {
        checkAttribute();
        return IndexProxy(owned_storage.get(), i);
    }
    
    void checkAttribute() {
        if (!valid) {
            throw std::runtime_error("Invalid attribute");
        }
    }
private:
    void invalidateAttribute() {
        valid = false;
    }
    
private:
    std::shared_ptr<NodeAttributeStorage<T>> owned_storage;
    bool valid;
    friend NodeAttributeStorage<T>;
}; // class NodeAttribute


class NodeAttributeMap {
    std::unordered_map<
    std::string_view,
    std::shared_ptr<NodeAttributeStorageBase>
    > attrMap;
    
public:
    auto find(std::string_view const& name) {
        auto it = attrMap.find(name);
        if(it == attrMap.end()) {
            throw std::runtime_error("No such attribute");
        }
        return it;
    }
    
    template<typename T>
    auto attach(std::string_view name) {
        auto ownedPtr = std::make_shared<NodeAttributeStorage<T>>(std::string{name});
        auto [it, success] = attrMap.insert(
                                            std::make_pair(ownedPtr->getName(), ownedPtr));
        if(!success) {
            throw std::runtime_error("Attribute with same name already exists");
        }
        return NodeAttribute<T>{ownedPtr};
    }
    
    void detach(std::string_view name) {
        auto it = find(name);
        auto storage = it->second.get();
        storage->invalidateAttributes();
        it->second.reset();
        attrMap.erase(name);
    }
    
    template<typename T>
    auto get(std::string_view name) {
        auto it = find(name);
        if (it->second.get()->getType() != typeid(T))
            throw std::runtime_error("Type mismatch in nodeAttributes().get()");
        return NodeAttribute<T>{std::static_pointer_cast<NodeAttributeStorage<T>>(it->second)};
    }
    
    void enumerate() {
        for (auto& [name, ptr] : attrMap) {
            std::cout<<name<<"\n";
        }
    }
}; //class NodeAttributeMap

} // namespace Attributes

#endif /* Attributes_h */
