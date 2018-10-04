"""
Keeps track of events such as function calls, communication and processor counters as streamed from adios 
Authors: Shinjae Yoo (sjyoo@bnl.gov), Gyorgy Matyasfalvi (gmatyasfalvi@bnl.gov)
Create: August, 2018
"""

from collections import deque
from collections import defaultdict
import configparser
import numpy as np

class  Event():
    def __init__(self, funMap, configFile):
      self.funstack = {} # A dictionary of function calls; keys: program, mpi rank, thread
      self.funmap = funMap # A dictionary of function ids; key: function id
      self.funtime = {} # A result dictionary containing a list for each function id which has the execution time; key: function id, 
      #self.funList = None # or initialize to []
      self.maxFunDepth = defaultdict(int)
      self.funData = None
      self.funDataTemp = None
      self.countData = None
      self.commData = None
      self.fidx = 0
      self.ctidx = 0
      self.coidx = 0
    
    
    def checkCallStack(self, p, r, t): # As events arrive build necessary data structure
      if p not in self.funstack:
        self.funstack[p] = {}
      if r not in self.funstack[p]:
        self.funstack[p][r] = {}
      if t not in self.funstack[p][r]:
        self.funstack[p][r][t] = deque()
   
    
    
    def addFun(self, event): # adds function call to call stack
      # Input event array is as obtained from the adios module's "event_timestamps" variable i.e.: 
      # [program, mpi rank, thread, entry/exit, function id, timestamp]
      self.checkCallStack(event[0],event[1],event[2]) # Make sure program, mpi rank, thread data structure is built
      
      if event[3] == 0:
        self.funstack[event[0]][event[1]][event[2]].append(event) # If entry event, add event to call stack
        self.funData[self.fidx,0:5] = event[0:5]
        self.funData[self.fidx,11:13] = event[5:7]
        self.fidx += 1
        return True
      elif event[3] == 1:
        pevent = self.funstack[event[0]][event[1]][event[2]].pop() # If exit event, remove corresponding entry event from call stack
        if pevent[4] != event[4] or pevent[5] > event[5]:
          print("entry event:", pevent)
          print("exit event:", event)
          #raise Exception("\nCall Stack Violation!\n")
          return False
        self.funData[self.fidx,0:5] = event[0:5]
        self.funData[self.fidx,11:13] = event[5:7]
        self.fidx += 1
        if event[4] not in self.maxFunDepth:  
            self.maxFunDepth[event[4]] = len(self.funstack[event[0]][event[1]][event[2]])
        else:
            if len(self.funstack[event[0]][event[1]][event[2]]) > self.maxFunDepth[event[4]]:
                self.maxFunDepth[event[4]] = len(self.funstack[event[0]][event[1]][event[2]])
                
        if pevent[4] in self.funmap: # If event corresponds to a function call of interest compute execution time
          if pevent[4] not in self.funtime:
            self.funtime[pevent[4]] = [] # If function id is new to results dictionary create list 
          self.funtime[pevent[4]].append([event[0], event[1], event[2], event[4], pevent[5], event[5] - pevent[5], pevent[6]])
          #self.funList.append(pevent)
          #self.funList.append(event)
          return True
      else:
        return True # Event is not an exit or entry event
    
    #def initFunData(self, numEvent):
    #    self.funList = []
        
    def initFunData(self, numEvent):
        self.funData = np.full((numEvent, 13), np.nan)
        
    def getFunData(self):
        #self.funList.sort(key=lambda x: x[6])
        #self.funData = np.full((len(self.funList), 13), np.nan)
        #self.funDataTemp = np.array(self.funList)
        #self.funData[:,0:5] = self.funDataTemp[:,0:5]
        #self.funData[:,11:13] = self.funDataTemp[:,5:7]
        return self.funData
    
    def clearFunData(self):
        self.fidx = 0
        #self.funList.clear()
        if self.funDataTemp is None:
            pass
        else:
            del self.funDataTemp
            
        if self.funData is None:
            pass
        else:
            del self.funData
        
        
    def addCount(self, event):       
        self.countData[self.ctidx,0:3] = event[0:3]
        self.countData[self.ctidx,5:7] = event[3:5]
        self.countData[self.ctidx,11:13] = event[5:7]
        self.ctidx += 1
        return True
        
    def initCountData(self, numEvent):
        self.countData = np.full((numEvent, 13), np.nan)
    
    def getCountData(self):
        return self.countData
    
    def clearCountData(self):
        self.ctidx = 0
        del self.countData
        
        
    def addComm(self, event):       
        self.commData[self.coidx,0:4] = event[0:4]
        self.commData[self.coidx,8:11] = event[4:7]
        self.commData[self.coidx,11:13] = event[7:9]
        self.coidx += 1
        return True
        
    def initCommData(self, numEvent):
        self.commData = np.full((numEvent, 13), np.nan)
    
    def getCommData(self):
        return self.commData
    
    def clearCommData(self):
        self.coidx = 0
        del self.commData
    
    
    def getFunExecTime(self): # Returns the result dictionary
        if len(self.funtime) > 0: 
            return self.funtime
        else:
            raise Exception("\nNo result dictionary!\n")
    
    
    def clearFunDict(self):
        self.funtime.clear()
    

    def getFunStack(self):
        return self.funstack

    
    def getFunStackSize(self):
        return len(self.funstack)
    
    
    def getMaxFunDepth(self):
        return self.maxFunDepth
    

    def printFunStack(self):
       print("self.funstack = ", self.funstack)

    