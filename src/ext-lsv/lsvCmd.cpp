#include "base/abc/abc.h"
#include "base/main/main.h"
#include "base/main/mainInt.h"
#include <vector>
#include <set>
#include <map>
#include <algorithm>

static int Lsv_CommandPrintNodes(Abc_Frame_t* pAbc, int argc, char** argv);
static int Lsv_CommandPrintMoCut(Abc_Frame_t* pAbc, int argc, char** argv);

void init(Abc_Frame_t* pAbc) {
  Cmd_CommandAdd(pAbc, "LSV", "lsv_print_nodes", Lsv_CommandPrintNodes, 0);
  Cmd_CommandAdd(pAbc, "LSV", "lsv_printmocut", Lsv_CommandPrintMoCut, 0);
}

void destroy(Abc_Frame_t* pAbc) {}

Abc_FrameInitializer_t frame_initializer = {init, destroy};

struct PackageRegistrationManager {
  PackageRegistrationManager() { Abc_FrameAddInitializer(&frame_initializer); }
} lsvPackageRegistrationManager;

void Lsv_NtkPrintNodes(Abc_Ntk_t* pNtk) {
  Abc_Obj_t* pObj;
  int i;
  Abc_NtkForEachNode(pNtk, pObj, i) {
    printf("Object Id = %d, name = %s\n", Abc_ObjId(pObj), Abc_ObjName(pObj));
    Abc_Obj_t* pFanin;
    int j;
    Abc_ObjForEachFanin(pObj, pFanin, j) {
      printf("  Fanin-%d: Id = %d, name = %s\n", j, Abc_ObjId(pFanin),
             Abc_ObjName(pFanin));
    }
    if (Abc_NtkHasSop(pNtk)) {
      printf("The SOP of this node:\n%s", (char*)pObj->pData);
    }
  }
}

int Lsv_CommandPrintNodes(Abc_Frame_t* pAbc, int argc, char** argv) {
  Abc_Ntk_t* pNtk = Abc_FrameReadNtk(pAbc);
  int c;
  Extra_UtilGetoptReset();
  while ((c = Extra_UtilGetopt(argc, argv, "h")) != EOF) {
    switch (c) {
      case 'h':
        goto usage;
      default:
        goto usage;
    }
  }
  if (!pNtk) {
    Abc_Print(-1, "Empty network.\n");
    return 1;
  }
  Lsv_NtkPrintNodes(pNtk);
  return 0;

usage:
  Abc_Print(-2, "usage: lsv_print_nodes [-h]\n");
  Abc_Print(-2, "\t        prints the nodes in the network\n");
  Abc_Print(-2, "\t-h    : print the command usage\n");
  return 1;
}

// Type definitions for cut enumeration
typedef std::set<unsigned int> Cut_t;
typedef std::vector<Cut_t> CutList_t;
typedef std::map<unsigned int, CutList_t> NodeCuts_t;

// Helper function to merge two cuts
Cut_t MergeCuts(const Cut_t& cut1, const Cut_t& cut2, int k) {
  Cut_t merged;
  std::set_union(cut1.begin(), cut1.end(),
                 cut2.begin(), cut2.end(),
                 std::inserter(merged, merged.begin()));

  // Return empty cut if size exceeds k
  if (merged.size() > k) {
    return Cut_t();
  }
  return merged;
}

// Check if cut1 is a subset of cut2 (for redundancy check)
bool IsSubset(const Cut_t& cut1, const Cut_t& cut2) {
  return std::includes(cut2.begin(), cut2.end(), cut1.begin(), cut1.end());
}

// Remove redundant cuts (keep only irredundant ones)
void RemoveRedundantCuts(CutList_t& cuts) {
  CutList_t result;

  for (const auto& cut : cuts) {
    bool isRedundant = false;

    // Check if this cut is redundant (superset of existing cut)
    for (const auto& existing : result) {
      if (IsSubset(existing, cut)) {
        isRedundant = true;
        break;
      }
    }

    if (!isRedundant) {
      // Remove existing cuts that are supersets of this cut
      result.erase(std::remove_if(result.begin(), result.end(),
                                  [&cut](const Cut_t& existing) {
                                    return IsSubset(cut, existing);
                                  }), result.end());
      result.push_back(cut);
    }
  }

  cuts = result;
}

// Enumerate cuts for a single node
CutList_t EnumerateNodeCuts(Abc_Obj_t* pNode, const NodeCuts_t& nodeCuts, int k) {
  CutList_t cuts;

  // For primary inputs, the only cut is the node itself
  if (Abc_ObjIsPi(pNode)) {
    cuts.push_back({Abc_ObjId(pNode)});
    return cuts;
  }

  // For AND nodes, enumerate cuts from fanin combinations
  if (Abc_AigNodeIsAnd(pNode)) {
    Abc_Obj_t* pFanin0 = Abc_ObjFanin0(pNode);
    Abc_Obj_t* pFanin1 = Abc_ObjFanin1(pNode);

    unsigned int fanin0Id = Abc_ObjId(pFanin0);
    unsigned int fanin1Id = Abc_ObjId(pFanin1);

    // Add trivial cut (node itself)
    cuts.push_back({Abc_ObjId(pNode)});

    // Get cuts from fanins
    auto it0 = nodeCuts.find(fanin0Id);
    auto it1 = nodeCuts.find(fanin1Id);

    if (it0 != nodeCuts.end() && it1 != nodeCuts.end()) {
      const CutList_t& cuts0 = it0->second;
      const CutList_t& cuts1 = it1->second;

      // Merge cuts from both fanins
      for (const auto& cut0 : cuts0) {
        for (const auto& cut1 : cuts1) {
          Cut_t merged = MergeCuts(cut0, cut1, k);
          if (!merged.empty()) {
            cuts.push_back(merged);
          }
        }
      }
    }

    // Remove redundant cuts
    RemoveRedundantCuts(cuts);
  }

  return cuts;
}

// Multi-output cut enumeration functions
void Lsv_NtkPrintMoCut(Abc_Ntk_t* pNtk, int k, int l) {
  // Check if network is AIG
  if (!Abc_NtkIsStrash(pNtk)) {
    Abc_Print(-1, "Network should be AIG (use 'strash' command first).\n");
    return;
  }

  NodeCuts_t nodeCuts;
  Abc_Obj_t* pObj;
  int i;

  // Step 1: Enumerate cuts for each node in topological order
  Abc_NtkForEachPi(pNtk, pObj, i) {
    nodeCuts[Abc_ObjId(pObj)] = EnumerateNodeCuts(pObj, nodeCuts, k);
  }

  Abc_NtkForEachNode(pNtk, pObj, i) {
    if (Abc_AigNodeIsAnd(pObj)) {
      nodeCuts[Abc_ObjId(pObj)] = EnumerateNodeCuts(pObj, nodeCuts, k);
    }
  }

  // Step 2: Find multi-output cuts
  std::map<Cut_t, std::vector<unsigned int>> cutToNodes;

  // Collect all cuts and the nodes that use them
  for (const auto& pair : nodeCuts) {
    unsigned int nodeId = pair.first;
    const CutList_t& cuts = pair.second;

    // Skip primary inputs for output (we want AND nodes only)
    Abc_Obj_t* pNode = Abc_NtkObj(pNtk, nodeId);
    if (!Abc_ObjIsPi(pNode)) {
      for (const auto& cut : cuts) {
        // Skip trivial cuts (single node)
        if (cut.size() > 1) {
          cutToNodes[cut].push_back(nodeId);
        }
      }
    }
  }

  // Step 3: Print multi-output cuts that satisfy the l constraint
  for (const auto& pair : cutToNodes) {
    const Cut_t& cut = pair.first;
    const std::vector<unsigned int>& outputNodes = pair.second;

    if (outputNodes.size() >= static_cast<size_t>(l)) {
      // Print cut inputs (sorted)
      bool first = true;
      for (unsigned int inputId : cut) {
        if (!first) printf(" ");
        printf("%u", inputId);
        first = false;
      }

      printf(" :");

      // Print output nodes (sorted)
      std::vector<unsigned int> sortedOutputs = outputNodes;
      std::sort(sortedOutputs.begin(), sortedOutputs.end());
      for (unsigned int outputId : sortedOutputs) {
        printf(" %u", outputId);
      }

      printf("\n");
    }
  }
}

int Lsv_CommandPrintMoCut(Abc_Frame_t* pAbc, int argc, char** argv) {
  Abc_Ntk_t* pNtk = Abc_FrameReadNtk(pAbc);
  int c, k = 3, l = 2;

  Extra_UtilGetoptReset();
  while ((c = Extra_UtilGetopt(argc, argv, "h")) != EOF) {
    switch (c) {
      case 'h':
        goto usage;
      default:
        goto usage;
    }
  }

  // Parse k and l parameters
  if (argc != globalUtilOptind + 2) {
    Abc_Print(-1, "Wrong number of arguments.\n");
    goto usage;
  }

  k = atoi(argv[globalUtilOptind]);
  l = atoi(argv[globalUtilOptind + 1]);

  if (k < 3 || k > 6 || l < 1 || l > 4) {
    Abc_Print(-1, "Invalid parameters: k should be 3-6, l should be 1-4.\n");
    return 1;
  }

  if (!pNtk) {
    Abc_Print(-1, "Empty network.\n");
    return 1;
  }

  Lsv_NtkPrintMoCut(pNtk, k, l);
  return 0;

usage:
  Abc_Print(-2, "usage: lsv_printmocut <k> <l>\n");
  Abc_Print(-2, "\t        enumerate k-l multi-output cuts in AIG\n");
  Abc_Print(-2, "\t<k>   : cut size limit (3-6)\n");
  Abc_Print(-2, "\t<l>   : minimum output sharing (1-4)\n");
  return 1;
}