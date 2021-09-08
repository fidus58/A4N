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
class NodeAttributeView;

// Base class for all node attributes.
class NodeAttributeStorageBase {
public:
    NodeAttributeStorageBase(std::string name, std::type_index type)
    : name{name}, type{type} { }

    virtual ~NodeAttributeStorageBase() = default;

    virtual void invalidateView() = 0;
    
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
    }

protected:
    void markValid(index i) {
        if(i >= valid.size()) {
            valid.resize(i + 1);
        }
        valid[i] = true;
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
};

template<typename T>
struct NodeAttributeStorage : NodeAttributeStorageBase {
    NodeAttributeStorage(std::string name)
    : NodeAttributeStorageBase{std::move(name), typeid(T)} { }

    ~NodeAttributeStorage() {
        myView->invalidateView();
        std::cerr<<"storage deleted\n";
    }
    
    void invalidateView() override {
        myView->invalidateView();
    }

    void resize(index i) {
        if(i >= values.size()) {
            values.resize(i + 1);
        }
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
    friend class NodeAttributeView<T>;
    NodeAttributeView<T>* myView;
};

class NodeAttributeLocation {
public:
    explicit NodeAttributeLocation(NodeAttributeStorageBase *storage)
    : storage{storage} { }

    template<typename T>
    NodeAttributeView<T> as();

private:
    NodeAttributeStorageBase *storage; // TODO: shared_ptr?
};

template<typename T>
class NodeAttributeView {
    
    class Iterator {
        NodeAttributeStorage<T>* storage;
        index idx;
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
    };
    
    class IndexProxy {
        NodeAttributeStorage<T>* storage;
        index idx;
    public:
        IndexProxy(NodeAttributeStorage<T> *storage, index idx)
        : storage{storage}, idx{idx} {}
        
        operator T() { // reading at idx
            storage->checkIndex(idx);
            return storage->values[idx];
        }
        
        T& operator=(T const& other) { // writing at idx
            storage->set(idx, std::move(other));
            return storage->values[idx];
        }
    };
public:
    explicit NodeAttributeView(NodeAttributeStorage<T> *storage)
    : storage{storage} {
        storage->myView = this;
    }
    
    auto begin() {
        return Iterator(storage).nextValid();
    }
    
    auto end() {
        return Iterator{nullptr};
    }
    
    NodeAttributeView(NodeAttributeView const & v)
    : storage{v.storage}, valid{v.valid} {};

    void set(index i, T v) {
        checkView();
        return storage->set(i, std::move(v));
    }
    
    auto get(index i) {
        checkView();
        storage->checkIndex(i);
        return storage->get(i);
    }
    
    IndexProxy operator[](index i) {
        checkView();
        return IndexProxy(storage, i);
    }

    void checkView() {
        if (!valid) {
            throw std::runtime_error("Invalid attribute view");
        }
    }
    
    void invalidateView() {
        valid = false;
    }
    
private:
    NodeAttributeStorage<T> *storage; // TODO: shared_ptr?
    bool valid = true;
};

/*
 i tried to do an explicit invalidation of views:
 
 you already had a solution which makes 'as' (with its double
 type check) unnecessary: just return the (only) view with the
 attribute attachment
 
 i think it is ok, that every attribute has a unique view
 but i see different view addresses with my local Xcode
 and in godbolt up to gcc 7.5 ... corrupting my view invalidation
 
 compiler bug? any idea?

 maybe, doing every view access with an additional check is bad
 in the first place,
 
 it should be ok, that (without these tedious checks) accessing
 a view after deleting the attribute could gives UB anyway
 
 the other option (shared_ptr) is not semantically correct either:
 after attribute deletion there should no access of a zombie view
 be allowed !?
 
 */

template<typename T>
NodeAttributeView<T> NodeAttributeLocation::as() {
    auto type = storage->getType();
    if(type != typeid(T)) {
        throw std::runtime_error("Type mismatch in Attribute::as()");
    }
    return NodeAttributeView<T>{static_cast<NodeAttributeStorage<T> *>(storage)};
}

class Graph {
    
    class NodeAttributes {
        std::unordered_map<
            std::string_view,
        // TODO: Maybe shared_ptr instead so that views do not become invalid after attribute is deleted?
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
        NodeAttributeView<T> attach(std::string_view name) {
            auto ownedPtr = std::make_unique<NodeAttributeStorage<T>>(std::string{name});
            auto borrowedPtr = ownedPtr.get();
            auto [it, success] = attrMap.insert(
                                std::make_pair(borrowedPtr->getName(), std::move(ownedPtr)));
            if(!success) {
                throw std::runtime_error("Attribute with same name already exists");
            }
            return NodeAttributeView<T>{borrowedPtr};
        }
        
        void detach(std::string_view name) {
            auto it = find(name);
            auto storage = it->second.get();
            storage->invalidateView();
            it->second.reset();
            attrMap.erase(name);
        }

        NodeAttributeLocation getNodeAttribute(std::string_view name) {
            auto it = find(name);
            return NodeAttributeLocation{it->second.get()};
        }
    
        void enumerate() {
            for (auto& [name, ptr] : attrMap) {
                std::cout<<name<<"\n";
            }
        }
    } nodeAttrs; //class NodeAttributes
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
   
    auto p = Point{42, 43};
    coords[210] = coords[211] = p;
/*
   index acces can differentiate between read and write access
   by IndexProxy:
 
   used left  of an assignment: writing the attribute by IndexProxy::operator =
   used right of an assignment: reading the attribute by IndexProxy::operator T
                                if T is a structured type this conversion needs
                                an explicit typecast
    
 */
    coords.set(21, p);
    Point p210 = coords[210];
    std::cout << "coords[210].x = " << p210.x << std::endl;
    std::cout << "coords[210].y = " << Point(coords[210]).y << std::endl;
    
    auto x = coords.get(21);
    if (x)
        std::cerr<<(*x).x<<"\n";
    else
        std::cerr<<"no value\n";
    // Whatis<decltype(x)> what;

    for (auto& c : coords){
        c.x = c.y = 666;
    }
    for (auto c : coords){
        std::cout<<"x = "<<c.x<<"\t y = "<<c.y<<"\n";
    }

    for (auto it = coords.begin(); it != coords.end(); ++it){
        std::cerr << "x = "<<(*it).x<<"\t y = "<<(*it).y<<"\n";
    }
    
    // Maybe nicer: operator[]

    // TODO: Deletion of attributes
    // TODO: Iterators over all attributes
    // TODO: Minimal Python interface (but not necessarily convenient functions etc.)

    // Later, after minimal PR:
    // TODO: Attributes for edges
    // TODO: Attributes for the entire graph
    // TODO: Need to serialize/deserialize attributes.
    //       Support in NetworKit's binary format would be nice (at least for int attributes)
    //       Support reading/writing attributes from separate files?
    //       User needs to pass functions to serialize complicated classes?
    //       Want to integrate an existing serialization framework?
    // TODO: In Python: maybe "virtual" interface without .as()?
    G.nodeAttributes().detach("Coordinates");
    auto c1 = G.nodeAttributes().attach<double>("Coordinates");
    c1[100] = 333.33;
    std::cerr<<c1[100]<<"\n";
    colors[0] = 33;
    std::cerr<<colors[0]<<"\n";
    G.nodeAttributes().enumerate();
}
