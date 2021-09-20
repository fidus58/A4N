#include <iostream>
#include <fstream>
#include <sstream>

#include "Attributes.hpp"

using namespace Attributes;

class Graph { // (substitute)
    NodeAttributeMap nodeAttrs;    
public:
    auto& nodeAttributes() { return nodeAttrs; }
}; // class Graph (substitute)

struct Point {
    double x;
    double y;
};

int main() {

    Graph G;
    
    auto colors = G.nodeAttributes().attach<int>("color");
    auto coords { G.nodeAttributes().attach<Point>("Coordinates") };
    auto coord2 { G.nodeAttributes().get<Point>("Coordinates") };

    // auto coo2 = coords;
    // auto colors2 = colors;
    // colors2.set(0, 3);
    auto p = Point{21, 42};
    coords[21] = p;
    coords[25] = Point{25, 50};
/*
   index acces can differentiate between read and write access
   by IndexProxy:
 
   used left  of an assignment: writing the attribute by IndexProxy::operator =
   used right of an assignment: reading the attribute by IndexProxy::operator T
                                if T is a structured type this conversion needs
                                an explicit typecast
    
 */
    coords.set(22, Point{22,44});
    Point p22 = coords[22];
    std::cout << "coords[22].x = " << p22.x << std::endl;
    std::cout << "coords[22].y = " << Point(coords[22]).y << std::endl;
    
    auto x = coords.get(23);
    if (x)
        std::cerr<<(*x).x<<"\n";
    else
        std::cerr<<"no value\n";
  
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
    
    /*
     
    std::vector<Point> save(coords.size());
    
    std::copy(coords.begin(), coords.end(), save.begin());
    
    this works with my XCode, but with none of most other
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

    i=0;
    for (auto p : save){
        // i is NOT the node index !
        std::cout<<i++<<" "<<p.x<<" "<<p.y<<"\n";
    }
*/
    std::string filename{"coords.txt"};
    
    std::ofstream out(filename);
    if (!out)
        std::cerr<<"cannot open "<<filename<<" for writing\n";
    
    for (auto it = coords.begin(); it != coords.end(); ++it){
        auto [n,v] = it.nodeValuePair();
        out << n << "\t" << v.x << "\t" << v.y <<"\n";
    }
    out.close();
    
    G.nodeAttributes().detach("Coordinates");
    
    auto c1 = G.nodeAttributes().attach<Point>("Coordinates");
    
    std::ifstream in(filename);
    if (!in) {
        std::cerr<<"cannot open "<<filename<<" for reading\n";
    }
    while (true) {
        int n;
        double x, y;
        std::string line;
        std::getline(in, line);
        if (!in) break;
        std::istringstream istring(line);
        istring>>n>>x>>y;
        std::cout<<"got: "<<n<<" "<<x<<" "<<y<<"\n";
        Point p = {x,y};
        c1[n] = p;
    }
    
    for(auto c: c1) {
        std::cout<<c.x<<" "<<c.y<<"\n";
    }
    G.nodeAttributes().enumerate();
}
