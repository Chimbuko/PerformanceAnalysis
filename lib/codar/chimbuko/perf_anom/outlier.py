"""
Outlier detection class
Authors: Shinjae Yoo (sjyoo@bnl.gov), Gyorgy Matyasfalvi (gmatyasfalvi@bnl.gov)
Create: August, 2018
"""

import configparser
import numpy as np
from sklearn.neighbors import LocalOutlierFactor

class Outlier():
    def __init__(self, configFile):
        self.config = configparser.ConfigParser()
        self.config.read(configFile)
        
        # Lof parameters
        self.numNeighbors = int(self.config['Lof']['n_neighbors'])
        self.algorithm = self.config['Lof']['algorithm']
        self.leafSize = int(self.config['Lof']['leaf_size'])
        self.metric = self.config['Lof']['metric']
        self.p = int(self.config['Lof']['p'])
        if self.config['Lof']['metric_params'] == "None":
            self.metricParams = None
        self.contamination = float(self.config['Lof']['contamination'])
        self.numJobs = int(self.config['Lof']['n_jobs'])
        
        # Lof objects
        self.clf = None
        
        # Outliers and scores
        self.outl = None
        self.score = None
        
        # Data
        # self.data = None # For now it may be better if we pass the data to the methods   


    def maxTimeDiff(self, data): # determine which function has the biggest difference between min and max execution time
        maxDiffExecTime = 0
        maxFunId = None
        funtime = data.getFunExecTime()
        for ii in funtime:
            ll = funtime[ii]
            maxEvent = max(ll, key=lambda ll: ll[4]) 
            minEvent = min(ll, key=lambda ll: ll[4])
            diffExecTime = maxEvent[4] - minEvent[4]
            if(diffExecTime > maxDiffExecTime):
                maxDiffExecTime = diffExecTime
                maxFunId = ii
        return maxFunId
    
    
    
    def lofComp(self, data):
        self.clf = LocalOutlierFactor(self.numNeighbors, self.algorithm, self.leafSize, self.metric, self.p, self.metricParams, self.contamination, self.numJobs)
        self.outl = self.clf.fit_predict(data)
        self.score = -1.0 * self.clf.negative_outlier_factor_
    
    
    def getClf(self):
        return self.clf
    
        
    def getScore(self):
        if type(self.score) == None:
            raise Exception("No scores computed ...")
        else:
            return self.score
        
        
    def getOutlier(self):
        if type(self.outl) == None:
            raise Exception("No outliers computed ...")
        else:
            return self.outl
        
        
    def getContamination(self):
        return self.contamination
   
    