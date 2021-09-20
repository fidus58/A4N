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
    auto p = Point{21.1, 42.2};
    coords[21] = p;
    coords[25] = Point{25.5, 50.5};
/*
   index acces can differentiate between read and write access
   by IndexProxy:
 
   used left  of an assignment: writing the attribute by IndexProxy::operator =
   used right of an assignment: reading the attribute by IndexProxy::operator T
                                if T is a structured type this conversion needs
                                an explicit typecast
    
 */
    coords.set(22, Point{22.2,44.4});
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
    
    std::string filename{"coords.txt"};
    
    std::ofstream out(filename);
    if (!out)
        std::cerr<<"cannot open '"
                 <<filename<<"' for writing\n";
    
    for (auto it = coords.begin(); it != coords.end(); ++it){
        auto [n,v] = it.nodeValuePair();
        out << n << "\t" << v.x << "\t" << v.y <<"\n";
    }
    out.close();
    
    G.nodeAttributes().detach("Coordinates");
    
    auto c1 = G.nodeAttributes().attach<Point>("Coordinates");
    
    std::ifstream in(filename);
    if (!in) {
        std::cerr<<"cannot open '"
                 <<filename<<"' for reading\n";
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
