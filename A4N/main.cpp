#include <cstddef>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace Attributes {

using index = size_t;

template <typename T>
class NodeAttribute;

template <typename>
class Whatis;

// Base class for all node attributes.
class NodeAttributeStorageBase {
public:
    NodeAttributeStorageBase(std::string name, std::type_index type)
    : name{name}, type{type} { }

    virtual ~NodeAttributeStorageBase() = default;

    virtual void invalidateAttribute() = 0;
    
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
};

template<typename T>
class NodeAttributeStorage : public NodeAttributeStorageBase {
public:
    NodeAttributeStorage(std::string name)
    : NodeAttributeStorageBase{std::move(name), typeid(T)} { }

    ~NodeAttributeStorage() {
        invalidateAttribute();
        std::cerr<<"storage deleted\n";
    }
    
    void invalidateAttribute() override {
        myAttribute->invalidateAttribute();
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
    NodeAttribute<T>* myAttribute;
};


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
    };
    
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
    };
public:
    explicit NodeAttribute(NodeAttributeStorage<T> *storage)
    : storage{storage} {
        storage->myAttribute = this;
    }
    
    auto begin() {
        return Iterator(storage).nextValid();
    }
    
    auto end() {
        return Iterator(nullptr);
    }
    
    auto size() {
        return storage->size();
    }
    
    NodeAttribute(NodeAttribute const & v) // = delete;
    : storage{nullptr}, valid{false} {};
    /* ideally copies should be prohibited (there is only one
       valid 'View' which is invalidated when the underlying
       storage is deleted)
     
       to cope with a weird RVO bug in some compilers
       (doing a hidden copy anyway - even when deleted)
       any copy is marked invalid
     */

    void set(index i, T v) {
        checkAttribute();
        return storage->set(i, std::move(v));
    }
    
    auto get(index i) {
        checkAttribute();
        storage->checkIndex(i);
        return storage->get(i);
    }
    
    IndexProxy operator[](index i) {
        checkAttribute();
        return IndexProxy(storage, i);
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
    NodeAttributeStorage<T> *storage; // TODO: shared_ptr?
    bool valid = true;
    friend NodeAttributeStorage<T>;
};

class Graph {
    
    class NodeAttributeMap {
        std::unordered_map<
            std::string_view,
        // TODO: Maybe shared_ptr instead so that views do not become invalid after attribute is deleted?
        /*
           decision, not TODO this because:
           
           one attribute storage [NodeAttributeStorageBase] should be
           accessed by only one accessor [NodeAttribute] so the latter
           can be invalidated when the storage deceases,
         
           with shared_ptr it's unknown who the last accessor is, which
           has to be invalided !
         
           for getting storage heritage we could do a (e.g. vector) copy before
         */
            std::unique_ptr<NodeAttributeStorageBase>
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
        [[nodiscard]]
        NodeAttribute<T> attach(std::string_view name) {
            auto ownedPtr = std::make_unique<NodeAttributeStorage<T>>(std::string{name});
            auto borrowedPtr = ownedPtr.get();
            auto [it, success] = attrMap.insert(
                                std::make_pair(borrowedPtr->getName(), std::move(ownedPtr)));
            if(!success) {
                throw std::runtime_error("Attribute with same name already exists");
            }
            return NodeAttribute<T>{borrowedPtr};
        }
        
        void detach(std::string_view name) {
            auto it = find(name);
            auto storage = it->second.get();
            storage->invalidateAttribute();
            it->second.reset();
            attrMap.erase(name);
        }

        void enumerate() {
            for (auto& [name, ptr] : attrMap) {
                std::cout<<name<<"\n";
            }
        }
    } nodeAttrs; //class NodeAttributeMap
public:
    auto& nodeAttributes() { return nodeAttrs; }
}; // class Graph (substitute)

} // namespace Attributes

#include <iostream>

using namespace Attributes;

struct Point {
    double x;
    double y;
};

int main() {

    Graph G;
    
    auto colors = G.nodeAttributes().attach<int>("color");
    auto coords { G.nodeAttributes().attach<Point>("Coordinates") };
    
    // auto colors2 = colors;
    // colors2.set(0, 3);
    auto p = Point{41, 42};
    coords[21] = coords[25] = p;
/*
   index acces can differentiate between read and write access
   by IndexProxy:
 
   used left  of an assignment: writing the attribute by IndexProxy::operator =
   used right of an assignment: reading the attribute by IndexProxy::operator T
                                if T is a structured type this conversion needs
                                an explicit typecast
    
 */
    coords.set(22, p);
    Point p22 = coords[22];
    std::cout << "coords[22].x = " << p22.x << std::endl;
    std::cout << "coords[22].y = " << Point(coords[22]).y << std::endl;
    
    auto x = coords.get(21);
    if (x)
        std::cerr<<(*x).x<<"\n";
    else
        std::cerr<<"no value\n";
    // Whatis<decltype(x)> what;

    /*
    for (auto& c : coords){
        c.x = c.y = 666;
    }
    */
    
    for (auto c : coords){
        std::cout<<"x = "<<c.x<<"\t y = "<<c.y<<"\n";
    }
    auto i = 0;
    for (auto it = coords.begin(); it != coords.end(); ++it){
        std::cerr << ++i << ":\tx = "<<(*it).x<<"\t y = "<<(*it).y<<"\n";
    }
    
    // Maybe nicer: operator[]: DONE

    // TODO: Deletion of attributes: DONE
    // TODO: Iterators over all attributes: DONE
    //       all iterations should go only through
    //       valid elements (even when there a more
    //       values 0-allocated)  but their (node-)indicies
    //       are not stored and as such not available while
    //       iterating
    //       WHAT IS THE BEST WAY TO HANDLE THIS?
    // TODO: Minimal Python interface (but not necessarily
    //       convenient functions etc.): NEXT STEP with
    //       first networkit integration, first i'd like
    //       to discuss/rate/improve/change the current
    //       setup

    // Later, after minimal PR:
    // TODO: Attributes for edges
    // TODO: Attributes for the entire graph
    // TODO: Need to serialize/deserialize attributes.
    //       Support in NetworKit's binary format would be nice (at least for int attributes)
    //       Support reading/writing attributes from separate files?
    //       User needs to pass functions to serialize complicated classes?
    //       Want to integrate an existing serialization framework?
    // TODO: In Python: maybe "virtual" interface without .as()?
    
    std::vector<Point> save(coords.size());
    
    std::copy(coords.begin(), coords.end(), save.begin());
    
/*  this works with my XCode, but with none of most other
    compilers: i get instantiation errors in copy and assume
    they try to use a memcpy-variant, for which
    iterator_traits-infos are missing ... moreover usual
    (sequential) iterators can measure the diff end-begin
 
    but this iterator is non-sequential by design !?
 
 a user defined copy as:
 
 template<class InputIt, class OutputIt>
 OutputIt copy(InputIt first, InputIt last,
               OutputIt d_first)
 {
     while (first != last) {
         *d_first = *first;
         ++d_first; ++first; // no op++(int) just now
     }
     return d_first;
 }
 
 will even report conflicts with std::copy (wich seems to
 be visible without std:: on some systems)
 
 naming it Copy works - any idea ?
 */
    i=0;
    for (auto p : save){
        // i is NOT the node index !
        std::cout<<i++<<" "<<p.x<<" "<<p.y<<"\n";
    }
    G.nodeAttributes().detach("Coordinates");
    
    // auto y = coords[21]; fails with "Invalid attribute"
    auto c1 = G.nodeAttributes().attach<double>("Coordinates");
    c1[100] = 333.33;
    std::cerr<<c1[100]<<"\n";
    colors[0] = 33;
    std::cerr<<colors[0]<<"\n";
    for(auto c: c1) {
        std::cout<<c<<"\n";
    }
    G.nodeAttributes().enumerate();
}
